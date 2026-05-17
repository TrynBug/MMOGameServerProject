namespace GameDataGenerator
{
    // GameEnum.xlsx 1개 enum 값의 메타정보
    public class EnumValueInfo
    {
        public int IntValue { get; set; }
        public string ValueName { get; set; } = "";
        public string KoreanName { get; set; } = "";
    }

    // GameEnum.xlsx 1개 enum class의 메타정보
    public class EnumInfo
    {
        public string EnumName { get; set; } = "";      // 예: MonsterGrade
        public string FileGroup { get; set; } = "";     // 예: Monster -> GameEnum_Monster.h
        public List<EnumValueInfo> Values { get; set; } = new List<EnumValueInfo>();
    }
}
