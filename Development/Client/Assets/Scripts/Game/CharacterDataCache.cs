using System.Collections.Generic;
using DataStructures;

namespace Client.Game
{
    // 씬 간에 공유되어야 하는 유저/캐릭터 데이터를 보관하는 싱글톤 캐시.
    //
    // Login 씬에서 CharacterListNtf를 받으면 여기에 저장하고,
    // CharacterSelect 씬에서 꺼내서 UI에 표시한다.
    //
    // 일반 C# 싱글톤. MonoBehaviour가 아니므로 씬 전환과 무관하게 살아있다.
    public class CharacterDataCache
    {
        public static CharacterDataCache Instance { get; } = new CharacterDataCache();

        private CharacterDataCache() { }

        // 로그인 응답에서 받은 정보
        public long UserId { get; private set; }

        // 게임서버가 보내준 캐릭터 목록 (0개 이상)
        public IReadOnlyList<Character> Characters { get; private set; } = new List<Character>();

        public void SetUserId(long userId)
        {
            UserId = userId;
        }

        public void SetCharacters(IList<Character> characters)
        {
            // protobuf의 RepeatedField를 그대로 들고 있으면 외부에서 수정될 수 있으니 복사.
            Characters = new List<Character>(characters);
        }

        public void Clear()
        {
            UserId = 0;
            Characters = new List<Character>();
        }
    }
}
