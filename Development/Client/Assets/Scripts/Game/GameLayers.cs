namespace Client.Game
{
    // 프로젝트 전역 물리 레이어 규약.
    // 이 상수들의 인덱스는 Unity 레이어(ProjectSettings/TagManager.asset)와 반드시 일치해야 한다.
    //
    // 역할별로 레이어를 나눠 (1) 레이캐스트 마스크로 판정 대상을 좁히고,
    // (2) 필요 시 Physics 충돌 매트릭스로 상호작용 쌍(투사체×몬스터 등)을 제어한다.
    //
    //   Ground     : 걸을 수 있는 지형 표면(Unity Terrain). 둔덕도 같은 콜라이더라 여기 포함.
    //                → 클릭이동 목적지 판정 + 투사체 차단(둔덕 오르막).
    //   Obstacle   : 지형과 별개인 정적 차단물(바위/돌담/기둥/절벽). 투사체·(추후)시야 차단.
    //   Monster    : 몬스터 액터. 투사체 hit 판정 대상(OnTriggerEnter).
    //   Player     : 플레이어 캐릭터.
    //   Projectile : 스킬 투사체.
    public static class GameLayers
    {
        // 내장 레이어 (TagManager 기본값).
        public const int Default = 0;
        public const int Water = 4;
        public const int UI = 5;

        // 프로젝트 정의 레이어.
        public const int Ground = 6;
        public const int Obstacle = 7;
        public const int Monster = 8;
        public const int Player = 9;
        public const int Projectile = 10;

        // 투사체(및 추후 시야)를 막는 정적 지오메트리 = 지형 + 장애물.
        public static readonly int ProjectileBlockerMask = (1 << Ground) | (1 << Obstacle);

        // 클릭 이동 목적지를 잡는 표면 = 지형 + 장애물 + 수면.
        public static readonly int GroundClickMask = (1 << Ground) | (1 << Obstacle) | (1 << Water);
    }
}
