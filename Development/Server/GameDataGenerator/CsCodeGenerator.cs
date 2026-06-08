using System.Text;

namespace GameDataGenerator
{
    public class CsCodeGenerator
    {
        // ----------------------------------------------------------------
        // GameEnum_Xxx.cs 생성
        // 위치: <enumOutputDir>/GameEnum_<group>.cs
        // ----------------------------------------------------------------
        public static void GenerateEnumFiles(List<EnumInfo> allEnums, string enumOutputDir)
        {
            var groups = new Dictionary<string, List<EnumInfo>>();
            foreach (var e in allEnums)
            {
                if (!groups.TryGetValue(e.FileGroup, out var list))
                {
                    list = new List<EnumInfo>();
                    groups[e.FileGroup] = list;
                }
                list.Add(e);
            }

            foreach (var (fileGroup, enums) in groups)
            {
                string path = Path.Combine(enumOutputDir, $"GameEnum_{fileGroup}.cs");
                File.WriteAllText(path, BuildEnumFile(enums), Encoding.UTF8);
            }
        }

        private static string BuildEnumFile(List<EnumInfo> enums)
        {
            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.");
            sb.AppendLine("// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("namespace GameData");
            sb.AppendLine("{");

            for (int i = 0; i < enums.Count; i++)
            {
                var enumInfo = enums[i];
                sb.AppendLine($"    public enum E{enumInfo.EnumName}");
                sb.AppendLine("    {");
                foreach (var v in enumInfo.Values)
                {
                    string comment = string.IsNullOrEmpty(v.KoreanName) ? "" : $"  // {v.KoreanName}";
                    sb.AppendLine($"        {v.ValueName,-20} = {v.IntValue},{comment}");
                }
                sb.AppendLine($"        {"Max",-20}");
                sb.AppendLine("    }");
                if (i < enums.Count - 1)
                    sb.AppendLine();
            }

            sb.AppendLine("}");
            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // GameDataBase_Xxx.cs 생성 (항상 덮어씀)
        // 위치: <baseOutputDir>/GameDataBase_<tableName>.cs
        // ----------------------------------------------------------------
        public static void GenerateBaseFile(string tableName, List<ColumnInfo> allColumns, string baseOutputDir)
        {
            string path = Path.Combine(baseOutputDir, $"GameDataBase_{tableName}.cs");
            File.WriteAllText(path, BuildBaseFile(tableName, allColumns), Encoding.UTF8);
        }

        private static string BuildBaseFile(string tableName, List<ColumnInfo> columns)
        {
            var clientCols    = columns.Where(c => c.IsForClient).ToList();
            var indexedGroups = GetIndexedGroups(clientCols);

            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.");
            sb.AppendLine("// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("using System.Collections.Generic;");
            sb.AppendLine();
            sb.AppendLine("namespace GameData");
            sb.AppendLine("{");

            // GameDataBase_Xxx
            sb.AppendLine($"    // {tableName} 데이터 1개 행을 표현합니다.");
            sb.AppendLine($"    public class GameDataBase_{tableName} : GameDataBase");
            sb.AppendLine("    {");
            foreach (var col in clientCols)
            {
                string csType     = ToCsType(col);
                string defaultVal = ToCsDefaultValue(col);
                sb.AppendLine($"        public {csType,-20} {col.FieldName,-20} = {defaultVal};");
            }

            foreach (var (baseName, group) in indexedGroups)
            {
                sb.AppendLine();
                string csType  = ToCsType(group[0]);
                string noneVal = ToCsDefaultValue(group[0]);
                sb.AppendLine($"        public int Get{baseName}Count() => {group.Count};");
                sb.AppendLine($"        public {csType} Get{baseName}(int index)");
                sb.AppendLine("        {");
                sb.AppendLine("            switch (index)");
                sb.AppendLine("            {");
                for (int i = 0; i < group.Count; i++)
                    sb.AppendLine($"                case {i}: return {group[i].FieldName};");
                sb.AppendLine($"                default: return {noneVal};");
                sb.AppendLine("            }");
                sb.AppendLine("        }");
            }

            sb.AppendLine("    }");
            sb.AppendLine();

            // GameDataTableBase_Xxx
            sb.AppendLine($"    // {tableName} 데이터 파일 전체를 표현합니다.");
            sb.AppendLine($"    public class GameDataTableBase_{tableName} : GameDataTableBase");
            sb.AppendLine("    {");
            sb.AppendLine($"        protected static Dictionary<long, GameData_{tableName}> sm_dataMap = new();");
            sb.AppendLine();
            sb.AppendLine($"        public override string GetDataName() => \"{tableName}\";");
            sb.AppendLine();
            sb.AppendLine($"        public static GameData_{tableName} FindData(long key)");
            sb.AppendLine("        {");
            sb.AppendLine("            sm_dataMap.TryGetValue(key, out var data);");
            sb.AppendLine("            return data;");
            sb.AppendLine("        }");
            sb.AppendLine();
            sb.AppendLine($"        public static IReadOnlyDictionary<long, GameData_{tableName}> GetDataMap() => sm_dataMap;");
            sb.AppendLine();
            sb.AppendLine("        protected override bool MakeGameData(string line)");
            sb.AppendLine("        {");
            sb.AppendLine("            string[] fields = line.Split(',');");
            sb.AppendLine($"            GameData_{tableName} data = new GameData_{tableName}();");
            sb.AppendLine();

            // 클라이언트 컬럼 파싱 (인덱스 기반)
            for (int i = 0; i < clientCols.Count; i++)
            {
                var col    = clientCols[i];
                string parser = ToCsParser(col, i);
                sb.AppendLine($"            data.{col.FieldName} = {parser};");
            }

            sb.AppendLine();
            sb.AppendLine("            if (data.Key <= 0)");
            sb.AppendLine("                return false;");
            sb.AppendLine();
            sb.AppendLine("            if (sm_dataMap.ContainsKey(data.Key))");
            sb.AppendLine("                return false;");
            sb.AppendLine();
            sb.AppendLine("            if (!data.Initialize())");
            sb.AppendLine("                return false;");
            sb.AppendLine();
            sb.AppendLine("            sm_dataMap[data.Key] = data;");
            sb.AppendLine();
            sb.AppendLine("            return OnAddData(data);");
            sb.AppendLine("        }");
            sb.AppendLine();
            sb.AppendLine($"        protected virtual bool OnAddData(GameData_{tableName} data) => true;");
            sb.AppendLine("    }");
            sb.AppendLine("}");

            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // GameData_Xxx.cs 생성 (파일 없을 때만)
        // 위치: <customOutputDir>/GameData_<tableName>.cs
        // ----------------------------------------------------------------
        public static void GenerateDataFile(string tableName, string customOutputDir)
        {
            string path = Path.Combine(customOutputDir, $"GameData_{tableName}.cs");
            if (File.Exists(path))
                return;

            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.");
            sb.AppendLine("// 이후에는 사용자가 직접 수정할 수 있습니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("namespace GameData");
            sb.AppendLine("{");
            sb.AppendLine($"    public class GameData_{tableName} : GameDataBase_{tableName}");
            sb.AppendLine("    {");
            sb.AppendLine("        public bool Initialize()");
            sb.AppendLine("        {");
            sb.AppendLine("            // 데이터 1건을 읽은 직후 호출됨");
            sb.AppendLine("            return true;");
            sb.AppendLine("        }");
            sb.AppendLine();
            sb.AppendLine("        // 여기에 사용자가 추가할 필드, 메서드를 선언합니다.");
            sb.AppendLine("    }");
            sb.AppendLine();
            sb.AppendLine($"    public class GameDataTable_{tableName} : GameDataTableBase_{tableName}");
            sb.AppendLine("    {");
            sb.AppendLine($"        protected override bool OnAddData(GameData_{tableName} data)");
            sb.AppendLine("        {");
            sb.AppendLine("            // 데이터가 sm_dataMap 에 추가된 후 호출됨");
            sb.AppendLine("            return true;");
            sb.AppendLine("        }");
            sb.AppendLine();
            sb.AppendLine("        protected override bool OnLoadComplete()");
            sb.AppendLine("        {");
            sb.AppendLine("            // 전체 데이터 로드 완료 후 호출됨");
            sb.AppendLine("            // 예: 다른 테이블과의 참조 연결, 유효성 검증 등");
            sb.AppendLine("            return true;");
            sb.AppendLine("        }");
            sb.AppendLine();
            sb.AppendLine("        // 여기에 사용자가 추가할 멤버함수를 선언합니다.");
            sb.AppendLine("    }");
            sb.AppendLine("}");

            File.WriteAllText(path, sb.ToString(), Encoding.UTF8);
        }

        // ----------------------------------------------------------------
        // GameDataManagerBase.cs 생성 (merge 명령, 항상 덮어씀)
        // 위치: <managerOutputDir>/GameDataManagerBase.cs
        // ----------------------------------------------------------------
        public static void GenerateManagerFile(List<string> tableNames, string managerOutputDir)
        {
            File.WriteAllText(Path.Combine(managerOutputDir, "GameDataManagerBase.cs"), BuildManagerCs(tableNames), Encoding.UTF8);
        }

        private static string BuildManagerCs(List<string> tableNames)
        {
            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("namespace GameData");
            sb.AppendLine("{");
            sb.AppendLine("    public class GameDataManagerBase");
            sb.AppendLine("    {");
            sb.AppendLine("        public static bool LoadAllGameData(string csvPath)");
            sb.AppendLine("        {");
            foreach (var name in tableNames)
            {
                sb.AppendLine($"            var table_{name} = new GameDataTable_{name}();");
                sb.AppendLine($"            if (!table_{name}.LoadData(csvPath))");
                sb.AppendLine($"                return false;");
                sb.AppendLine();
            }
            sb.AppendLine("            return true;");
            sb.AppendLine("        }");
            sb.AppendLine("    }");
            sb.AppendLine("}");
            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // 헬퍼
        // ----------------------------------------------------------------
        private static string ToCsType(ColumnInfo col)
        {
            return col.DataType switch
            {
                "int32"  => "int",
                "int64"  => "long",
                "string" => "string",
                "float"  => "float",
                "double" => "double",
                "bool"   => "bool",
                "enum"   => $"E{col.DataTypeDetail}",
                _        => ""
            };
        }

        private static string ToCsDefaultValue(ColumnInfo col)
        {
            return col.DataType switch
            {
                "int32"  => string.IsNullOrEmpty(col.DefaultValue) ? "0" : col.DefaultValue,
                "int64"  => string.IsNullOrEmpty(col.DefaultValue) ? "0" : col.DefaultValue,
                "string" => $"\"{col.DefaultValue}\"",
                "float"  => string.IsNullOrEmpty(col.DefaultValue) ? "0f" : $"{col.DefaultValue}f",
                "double" => string.IsNullOrEmpty(col.DefaultValue) ? "0.0" : col.DefaultValue,
                "bool"   => col.DefaultValue.ToUpper() == "TRUE" ? "true" : "false",
                "enum"   => $"E{col.DataTypeDetail}.{(string.IsNullOrEmpty(col.DefaultValue) ? "None" : col.DefaultValue)}",
                _        => ""
            };
        }

        private static string ToCsParser(ColumnInfo col, int index)
        {
            return col.DataType switch
            {
                "int32"  => $"int.Parse(fields[{index}])",
                "int64"  => $"long.Parse(fields[{index}])",
                "string" => $"fields[{index}]",
                "float"  => $"float.Parse(fields[{index}])",
                "double" => $"double.Parse(fields[{index}])",
                "bool"   => $"GameDataTableBase.StringToBool(fields[{index}])",
                "enum"   => $"(E{col.DataTypeDetail})int.Parse(fields[{index}])",
                _        => ""
            };
        }

        private static Dictionary<string, List<ColumnInfo>> GetIndexedGroups(List<ColumnInfo> columns)
        {
            var groups = new Dictionary<string, List<ColumnInfo>>();
            foreach (var col in columns)
            {
                if (col.IndexSuffix < 0)
                    continue;
                if (!groups.TryGetValue(col.BaseFieldName, out var list))
                {
                    list = new List<ColumnInfo>();
                    groups[col.BaseFieldName] = list;
                }
                list.Add(col);
            }
            foreach (var list in groups.Values)
                list.Sort((a, b) => a.IndexSuffix.CompareTo(b.IndexSuffix));
            return groups;
        }
    }
}
