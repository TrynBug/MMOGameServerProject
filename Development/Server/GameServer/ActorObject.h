#pragma once

#include "pch.h"
#include "StageObject.h"

// ─────────────────────────────────────────────────────────────
// ActorObject 베이스 클래스
// ─────────────────────────────────────────────────────────────
//
// StageObject 중에서 "상호작용 가능한 오브젝트" 의 공통 베이스 클래스이다.
// Character, Monster, Pet, NPC 등이 이 클래스를 상속받는다.
//
// 예정된 공통 속성 (현재는 미구현):
//   - 스탯 (체력, 공격력, 이동속도 등)
//   - 버프 / 디버프
//   - 스킬
//   - AI
//   - 레벨
//   - 생존 / 사망 상태
//
// 단순 트리거(Trigger) 나 장식용 Prop 처럼 상호작용이 없는 오브젝트는
// 이 클래스를 상속받지 않고 StageObject 를 직접 상속받는다.
//
// 멤버 접근은 소속 Stage의 컨텐츠 스레드에서만 이루어진다.
// 그래서 별도의 락 없이 사용한다.
class ActorObject : public StageObject
{
public:
    ActorObject(int64 objectId, EObjectType objectType);
    ~ActorObject() override = default;

    ActorObject(const ActorObject&) = delete;
    ActorObject& operator=(const ActorObject&) = delete;
};

using ActorObjectPtr  = std::shared_ptr<ActorObject>;
using ActorObjectWPtr = std::weak_ptr<ActorObject>;
