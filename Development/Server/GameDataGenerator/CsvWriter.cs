using ClosedXML.Excel;
using System.Text;

namespace GameDataGenerator
{
    public class CsvWriter
    {
        // ----------------------------------------------------------------
        // xlsx 데이터 행을 읽어 csv로 출력
        // isServer=true 이면 server/all 컬럼 출력, false 이면 client/all 컬럼 출력
        // ----------------------------------------------------------------
        public static void Write(
            string xlsxPath,
            string tableName,
            List<ColumnInfo> columns,
            Dictionary<string, Dictionary<string, int>> enumValueMap,
            string outputDir,
            bool isServer)
        {
            var targetCols = isServer
                ? columns.Where(c => c.IsForServer).ToList()
                : columns.Where(c => c.IsForClient).ToList();

            using var workbook = new XLWorkbook(xlsxPath);
            var sheet = workbook.Worksheet(1);

            int lastRow = sheet.LastRowUsed()?.RowNumber() ?? 8;

            // xlsx에서 8행 기준으로 컬럼 인덱스(1-based) 매핑
            int lastCol = sheet.LastColumnUsed()?.ColumnNumber() ?? 2;
            var fieldToXlsxCol = new Dictionary<string, int>();
            for (int col = 2; col <= lastCol; col++)
            {
                string fieldName = sheet.Cell(8, col).GetString().Trim();
                if (!string.IsNullOrEmpty(fieldName))
                    fieldToXlsxCol[fieldName] = col;
            }

            var sb = new StringBuilder();

            // 1행: 헤더 (필드명)
            sb.AppendLine(string.Join(",", targetCols.Select(c => c.FieldName)));

            // 9행~마지막행: 데이터
            for (int row = 9; row <= lastRow; row++)
            {
                // A열 "//" 체크 (주석 행 제외)
                string aCell = sheet.Cell(row, 1).GetString().Trim();
                if (aCell.StartsWith("//"))
                    continue;

                // 빈 행 제외 (Key 컬럼이 비어있으면 스킵)
                var keyCol = targetCols.FirstOrDefault(c => c.FieldName == "Key");
                if (keyCol != null && fieldToXlsxCol.TryGetValue("Key", out int keyColIdx))
                {
                    string keyVal = sheet.Cell(row, keyColIdx).GetString().Trim();
                    if (string.IsNullOrEmpty(keyVal))
                        continue;
                }

                var rowValues = new List<string>();
                foreach (var col in targetCols)
                {
                    if (!fieldToXlsxCol.TryGetValue(col.FieldName, out int xlsxCol))
                    {
                        rowValues.Add(col.DefaultValue);
                        continue;
                    }

                    string rawVal = sheet.Cell(row, xlsxCol).GetString().Trim();

                    // 빈 값이면 기본값 사용
                    if (string.IsNullOrEmpty(rawVal))
                        rawVal = col.DefaultValue;

                    // enum이면 숫자값으로 변환
                    if (col.DataType == "enum" && !string.IsNullOrEmpty(col.DataTypeDetail))
                    {
                        if (enumValueMap.TryGetValue(col.DataTypeDetail, out var valueMap)
                            && valueMap.TryGetValue(rawVal, out int intVal))
                        {
                            rawVal = intVal.ToString();
                        }
                        else if (!int.TryParse(rawVal, out _))
                        {
                            // 변환 실패 시 0(None) 사용
                            rawVal = "0";
                        }
                    }

                    // bool 정규화
                    if (col.DataType == "bool")
                        rawVal = rawVal.ToUpper() == "TRUE" ? "true" : "false";

                    // 쉼표 포함 string은 따옴표로 감싸기
                    if (col.DataType == "string" && rawVal.Contains(','))
                        rawVal = $"\"{rawVal}\"";

                    rowValues.Add(rawVal);
                }

                sb.AppendLine(string.Join(",", rowValues));
            }

            string outputPath = Path.Combine(outputDir, $"{tableName}.csv");
            File.WriteAllText(outputPath, sb.ToString(), Encoding.UTF8);
        }
    }
}
