using System.Collections.Generic;
using DataStructures;
using UnityEngine;

namespace Client.Game
{
    // 씬 간에 공유되어야 하는 유저/캐릭터 데이터를 보관하는 싱글톤 캐시.
    //
    // Login 씬에서 CharacterListNtf를 받으면 Characters에 저장(선택 UI용 목록).
    // CharacterSelect 씬에서 CharacterSelectRes(성공)를 받으면 LocalCharacter(전체 데이터)와
    // SelectedStageDataKey를 저장한다.
    //
    // LocalCharacter는 "내 캐릭터 데이터 모델"이다. 스테이지 이동마다 새로 만들지 않고
    // 로그인~로그아웃 동안 계속 보관한다. 앞으로 인벤/장비/스킬 등이 붙으면 이 모델이 커진다.
    // (GameObject 표현물은 Game 씬의 StageManager가 이 모델로부터 1회 생성해 들고 다닌다.)
    //
    // 일반 C# 싱글톤. MonoBehaviour가 아니므로 씬 전환과 무관하게 살아있다.
    public class CharacterDataCache
    {
        public static CharacterDataCache Instance { get; } = new CharacterDataCache();

        private CharacterDataCache() { }

        // 로그인 응답에서 받은 정보
        public long AccountId { get; private set; }

        // 게임서버가 보내준 캐릭터 목록 (0개 이상). 선택 화면용 요약 목록.
        public IReadOnlyList<Character> Characters { get; private set; } = new List<Character>();

        // 선택된 내 캐릭터의 전체 데이터 모델 (CharacterSelectRes 성공 시 채워짐).
        // 영속: 스테이지 이동에도 유지된다.
        public Character LocalCharacter { get; private set; }

        // 최초 입장할 Stage 데이터 Key (CharacterSelectRes 의 stage_data_key).
        public int SelectedStageDataKey { get; private set; }

        public void SetAccountId(long accountId)
        {
            AccountId = accountId;
        }

        public void SetCharacters(IList<Character> characters)
        {
            // protobuf의 RepeatedField를 그대로 들고 있으면 외부에서 수정될 수 있으니 복사.
            Characters = new List<Character>(characters);
        }

        // CharacterSelectRes(성공) 수신 시 호출. 전체 데이터 모델을 깊은 복사로 보관한다.
        public void SetLocalCharacter(Character character, int stageDataKey)
        {
            LocalCharacter = character.Clone();   // protobuf 깊은 복사 (송신 버퍼와 분리)
            SelectedStageDataKey = stageDataKey;
        }

        public void Clear()
        {
            AccountId = 0;
            Characters = new List<Character>();
            LocalCharacter = null;
            SelectedStageDataKey = 0;
        }
    }
}
