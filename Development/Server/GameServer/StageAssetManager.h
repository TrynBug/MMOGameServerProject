#pragma once

#include "pch.h"
#include "StageLayout.h"

#include <unordered_map>
#include <string>

// ─────────────────────────────────────────────────────────────
// StageAssetManager
// ─────────────────────────────────────────────────────────────
//
// 게임서버 시작 시 모든 Stage 의 배치데이터(StageLayout)와 스크립트(컴파일된 바이트코드)를
// 한 번 로드/컴파일해 보관하고, 모든 Stage 인스턴스가 공유한다(불변).
//   - 레이아웃: stageDataKey 별 1개. 다인스턴스 던전이 디스크/파싱 중복 없이 공유.
//   - 스크립트: 이름별 바이트코드 1개. 각 Stage 의 lua_State 는 이 바이트코드를 "로드만"(파싱 생략).
//     단, lua_State(VM) 자체는 스레드 안전·인스턴스별 상태 때문에 공유 불가 → Stage 마다 1개.
//
// 검증(fail-fast): GameData_Stage.ScriptName# 에 명시된 .lua 파일이 없거나 컴파일 실패하면 LoadAll 이 false.
//                  레이아웃도 동일 — StageLayoutFileName 이 명시됐는데 .json 이 없거나 파싱 실패하면 false.
//                  (StageLayoutFileName 이 비어있으면 레이아웃 없는 Stage 로 정상 허용.)
//
// 스레드: 시작 시 1회 로드 후 불변. 멀티 컨텐츠 스레드가 락 없이 동시 읽기 안전(NavMesh/GameData 와 동일).
class StageAssetManager
{
public:
    // GameData_Stage 전체를 순회하며 레이아웃 로드 + 명시된 스크립트 컴파일/검증.
    // 명시된 스크립트 파일 누락/컴파일 실패가 하나라도 있으면 false (서버 시작 중단).
    bool LoadAll();

    // stageDataKey 의 공유 불변 레이아웃. 등록 안 된 키면 nullptr (정상 케이스에선 항상 존재).
    const StageLayout* FindLayout(int32 stageDataKey) const;

    // 스크립트 이름의 컴파일된 바이트코드. 없으면 nullptr.
    const std::string* FindScriptBytecode(const std::string& name) const;

private:
    bool compileScript(const std::string& name);   // 파일 없음/컴파일 실패면 false

    std::unordered_map<int32, StageLayout>       m_layouts;        // stageDataKey -> 레이아웃(불변)
    std::unordered_map<std::string, std::string> m_scriptBytecode; // 스크립트 이름 -> 바이트코드(불변)
};
