namespace GameDataGenerator
{
    // xlsx 1개 컬럼의 메타정보
    public class ColumnInfo
    {
        public string FieldName { get; set; } = "";     // 8행: 필드명
        public string Target { get; set; } = "none";    // 4행: all / server / client / none
        public string DataType { get; set; } = "";      // 5행: int / string / double / bool / enum
        public string DataTypeDetail { get; set; } = ""; // 6행: enum일 때 enum class 이름
        public string DefaultValue { get; set; } = "";  // 7행: 기본값

        // 필드명 끝 숫자 (예: Stat1 -> 1, Stat2 -> 2, 없으면 -1)
        public int IndexSuffix { get; set; } = -1;

        // 필드명에서 숫자를 뺀 기본 이름 (예: Stat1 -> Stat)
        public string BaseFieldName { get; set; } = "";

        public bool IsForServer => Target == "all" || Target == "server";
        public bool IsForClient => Target == "all" || Target == "client";
    }
}
