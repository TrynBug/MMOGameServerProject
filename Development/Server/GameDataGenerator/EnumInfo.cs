namespace GameDataGenerator
{
    // GameEnum.xlsx 1개 enum 값의 메타정보
    public class EnumValueInfo
    {
        public int IntValue { get; set; }
        public string ValueName { get; set; } = "";
        public string KoreanName { get; set; } = "";
        public string Description { get; set; } = "";   // G열 값 설명
    }

    // GameEnum.xlsx 1개 enum class의 메타정보
    public class EnumInfo
    {
        public string EnumName { get; set; } = "";      // 예: MonsterGrade
        public string Description { get; set; } = "";    // B열 Enum 설명 (선언부 위 블록 주석)
        public string FileGroup { get; set; } = "";     // 예: Monster -> GameEnum_Monster.h
        public List<EnumValueInfo> Values { get; set; } = new List<EnumValueInfo>();
    }
}
