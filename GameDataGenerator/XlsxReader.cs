using ClosedXML.Excel;
using System.Text.RegularExpressions;

namespace GameDataGenerator
{
    public class XlsxReader
    {
        // ----------------------------------------------------------------
        // GameEnum.xlsx 파싱
        // ----------------------------------------------------------------
        public static List<EnumInfo> ReadEnumXlsx(string xlsxPath)
        {
            var result = new List<EnumInfo>();
            var enumMap = new Dictionary<string, EnumInfo>();

            using var workbook = new XLWorkbook(xlsxPath);
            var sheet = workbook.Worksheet(1);

            // 1행은 컬럼명(헤더) -> 2행부터 읽기
            int lastRow = sheet.LastRowUsed()?.RowNumber() ?? 1;
            for (int row = 2; row <= lastRow; row++)
            {
                string enumName = sheet.Cell(row, 1).GetString().Trim();
                string fileGroup = sheet.Cell(row, 2).GetString().Trim();
                string intValueStr = sheet.Cell(row, 3).GetString().Trim();
                string valueName = sheet.Cell(row, 4).GetString().Trim();
                string koreanName = sheet.Cell(row, 5).GetString().Trim();

                if (string.IsNullOrEmpty(enumName) || string.IsNullOrEmpty(valueName))
                    continue;

                if (!int.TryParse(intValueStr, out int intValue))
                    continue;

                if (!enumMap.TryGetValue(enumName, out var enumInfo))
                {
                    enumInfo = new EnumInfo { EnumName = enumName, FileGroup = fileGroup };
                    enumMap[enumName] = enumInfo;
                    result.Add(enumInfo);
                }

                enumInfo.Values.Add(new EnumValueInfo
                {
                    IntValue = intValue,
                    ValueName = valueName,
                    KoreanName = koreanName
                });
            }

            return result;
        }

        // ----------------------------------------------------------------
        // 게임데이터 .xlsx 파싱
        // ----------------------------------------------------------------
        public static (string tableName, List<ColumnInfo> columns) ReadDataXlsx(string xlsxPath)
        {
            using var workbook = new XLWorkbook(xlsxPath);
            var sheet = workbook.Worksheet(1);

            // 1행 B열: 데이터파일 이름
            string tableName = sheet.Cell(1, 2).GetString().Trim();

            // 4~8행: 컬럼 메타정보 읽기 (B열부터)
            int lastCol = sheet.LastColumnUsed()?.ColumnNumber() ?? 2;
            var columns = new List<ColumnInfo>();

            for (int col = 2; col <= lastCol; col++)
            {
                string target = sheet.Cell(4, col).GetString().Trim().ToLower();
                if (string.IsNullOrEmpty(target))
                    target = "none";
                if (target == "none")
                    continue;

                string dataType = sheet.Cell(5, col).GetString().Trim().ToLower();
                if (string.IsNullOrEmpty(dataType))
                    continue;

                string dataTypeDetail = sheet.Cell(6, col).GetString().Trim();
                string defaultValue = sheet.Cell(7, col).GetString().Trim();
                string fieldName = sheet.Cell(8, col).GetString().Trim();

                if (string.IsNullOrEmpty(fieldName))
                    continue;

                var colInfo = new ColumnInfo
                {
                    FieldName = fieldName,
                    Target = target,
                    DataType = dataType,
                    DataTypeDetail = dataTypeDetail,
                    DefaultValue = defaultValue,
                    BaseFieldName = fieldName,
                    IndexSuffix = -1
                };

                // 필드명 끝 숫자 감지 (예: Stat1 -> base=Stat, suffix=1)
                var match = Regex.Match(fieldName, @"^(.*?)(\d+)$");
                if (match.Success)
                {
                    colInfo.BaseFieldName = match.Groups[1].Value;
                    colInfo.IndexSuffix = int.Parse(match.Groups[2].Value);
                }

                columns.Add(colInfo);
            }

            return (tableName, columns);
        }

        // ----------------------------------------------------------------
        // enum 이름 -> 정수값 변환 맵 빌드 (csv 생성 시 사용)
        // ----------------------------------------------------------------
        public static Dictionary<string, Dictionary<string, int>> BuildEnumValueMap(List<EnumInfo> enums)
        {
            var map = new Dictionary<string, Dictionary<string, int>>();
            foreach (var enumInfo in enums)
            {
                var valueMap = new Dictionary<string, int>();
                foreach (var v in enumInfo.Values)
                    valueMap[v.ValueName] = v.IntValue;
                map[enumInfo.EnumName] = valueMap;
            }
            return map;
        }
    }
}
