using System.Text;

namespace GameDataGenerator
{
    public class CppCodeGenerator
    {
        // ----------------------------------------------------------------
        // GameEnum_Xxx.h + GameEnum_Xxx.cpp 생성
        // ----------------------------------------------------------------
        public static void GenerateEnumFiles(List<EnumInfo> allEnums, string outputDir)
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
                string headerPath = Path.Combine(outputDir, $"GameEnum_{fileGroup}.h");
                string cppPath = Path.Combine(outputDir, $"GameEnum_{fileGroup}.cpp");

                File.WriteAllText(headerPath, BuildEnumHeader(fileGroup, enums), Encoding.UTF8);
                File.WriteAllText(cppPath, BuildEnumCpp(fileGroup, enums), Encoding.UTF8);
            }
        }

        private static string BuildEnumHeader(string fileGroup, List<EnumInfo> enums)
        {
            var sb = new StringBuilder();
            sb.AppendLine("#pragma once");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.");
            sb.AppendLine("// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("#include <string>");
            sb.AppendLine();

            foreach (var enumInfo in enums)
            {
                sb.AppendLine($"enum class E{enumInfo.EnumName} : int");
                sb.AppendLine("{");
                foreach (var v in enumInfo.Values)
                {
                    string comment = string.IsNullOrEmpty(v.KoreanName) ? "" : $"  // {v.KoreanName}";
                    sb.AppendLine($"    {v.ValueName,-20} = {v.IntValue},{comment}");
                }
                sb.AppendLine($"    {"Max",-20}");
                sb.AppendLine("};");
                sb.AppendLine();
                sb.AppendLine($"E{enumInfo.EnumName} StringTo{enumInfo.EnumName}(const std::string& v);");
                sb.AppendLine($"std::string {enumInfo.EnumName}ToString(E{enumInfo.EnumName} v);");
                sb.AppendLine();
            }

            return sb.ToString();
        }

        private static string BuildEnumCpp(string fileGroup, List<EnumInfo> enums)
        {
            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.");
            sb.AppendLine("// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine($"#include \"GameEnum_{fileGroup}.h\"");
            sb.AppendLine();

            foreach (var enumInfo in enums)
            {
                sb.AppendLine($"E{enumInfo.EnumName} StringTo{enumInfo.EnumName}(const std::string& v)");
                sb.AppendLine("{");
                foreach (var v in enumInfo.Values)
                    sb.AppendLine($"    if (v == \"{v.ValueName}\") return E{enumInfo.EnumName}::{v.ValueName};");
                sb.AppendLine($"    return E{enumInfo.EnumName}::None;");
                sb.AppendLine("}");
                sb.AppendLine();

                sb.AppendLine($"std::string {enumInfo.EnumName}ToString(E{enumInfo.EnumName} v)");
                sb.AppendLine("{");
                sb.AppendLine("    switch (v)");
                sb.AppendLine("    {");
                foreach (var v in enumInfo.Values)
                    sb.AppendLine($"    case E{enumInfo.EnumName}::{v.ValueName}: return \"{v.ValueName}\";");
                sb.AppendLine("    default: return \"None\";");
                sb.AppendLine("    }");
                sb.AppendLine("}");
                sb.AppendLine();
            }

            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // GameDataBase_Xxx.h + GameDataBase_Xxx.cpp 생성 (항상 덮어씀)
        // ----------------------------------------------------------------
        public static void GenerateBaseFiles(string tableName, List<ColumnInfo> allColumns, List<EnumInfo> allEnums, string outputDir)
        {
            File.WriteAllText(Path.Combine(outputDir, $"GameDataBase_{tableName}.h"),   BuildBaseHeader(tableName, allColumns, allEnums), Encoding.UTF8);
            File.WriteAllText(Path.Combine(outputDir, $"GameDataBase_{tableName}.cpp"), BuildBaseCpp(tableName, allColumns),              Encoding.UTF8);
        }

        private static string BuildBaseHeader(string tableName, List<ColumnInfo> columns, List<EnumInfo> allEnums)
        {
            var serverCols     = columns.Where(c => c.IsForServer).ToList();
            var indexedGroups  = GetIndexedGroups(serverCols);

            // 이 테이블이 사용하는 enum의 fileGroup만 include
            var usedEnumNames      = serverCols.Where(c => c.DataType == "enum" && !string.IsNullOrEmpty(c.DataTypeDetail)).Select(c => c.DataTypeDetail).ToHashSet();
            var enumNameToGroup    = allEnums.ToDictionary(e => e.EnumName, e => e.FileGroup);
            var requiredFileGroups = usedEnumNames.Where(n => enumNameToGroup.ContainsKey(n)).Select(n => enumNameToGroup[n]).Distinct().OrderBy(g => g).ToList();

            var sb = new StringBuilder();
            sb.AppendLine("#pragma once");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.");
            sb.AppendLine("// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("#include <map>");
            sb.AppendLine("#include <string>");
            sb.AppendLine();
            sb.AppendLine("#include \"LoggerLib.h\"");
            sb.AppendLine("#include \"../GameData.h\"");
            sb.AppendLine();
            foreach (var fileGroup in requiredFileGroups)
                sb.AppendLine($"#include \"Enum/GameEnum_{fileGroup}.h\"");
            sb.AppendLine();
            sb.AppendLine($"struct GameData_{tableName};");
            sb.AppendLine();
            sb.AppendLine();
            sb.AppendLine($"// {tableName} 데이터 1개 행을 표현합니다.");
            sb.AppendLine($"struct GameDataBase_{tableName} : public GameData");
            sb.AppendLine("{");

            foreach (var col in serverCols)
            {
                string cppType    = ToCppType(col);
                string defaultVal = ToCppDefaultValue(col);
                sb.AppendLine($"    {cppType,-20} {col.FieldName,-20} = {defaultVal};");
            }

            foreach (var (baseName, group) in indexedGroups)
            {
                sb.AppendLine();
                string cppType  = ToCppType(group[0]);
                string noneVal  = ToCppDefaultValue(group[0]);
                sb.AppendLine($"    int32_t Get{baseName}Count() const {{ return {group.Count}; }}");
                sb.AppendLine($"    {cppType} Get{baseName}(int32_t index) const");
                sb.AppendLine("    {");
                sb.AppendLine("        switch (index)");
                sb.AppendLine("        {");
                for (int i = 0; i < group.Count; i++)
                    sb.AppendLine($"        case {i}: return {group[i].FieldName};");
                sb.AppendLine($"        default: return {noneVal};");
                sb.AppendLine("        }");
                sb.AppendLine("    }");
            }

            sb.AppendLine("};");
            sb.AppendLine();
            sb.AppendLine();
            sb.AppendLine($"// {tableName} 데이터 파일 전체를 표현합니다.");
            sb.AppendLine($"class GameDataTableBase_{tableName} : public GameDataTable");
            sb.AppendLine("{");
            sb.AppendLine();
            sb.AppendLine("public:");
            sb.AppendLine($"    static constexpr const std::string_view k_dataName = \"{tableName}\";");
            sb.AppendLine();
            sb.AppendLine("protected:");
            sb.AppendLine($"    GameDataTableBase_{tableName}() = default;");
            sb.AppendLine($"    virtual ~GameDataTableBase_{tableName}() = default;");
            sb.AppendLine();
            sb.AppendLine("public:");
            sb.AppendLine($"    static const GameData_{tableName}* FindData(int64_t key);");
            sb.AppendLine($"    static const std::map<int64_t, const GameData_{tableName}*>& GetDataMap() {{ return sm_dataMap; }}");
            sb.AppendLine();
            sb.AppendLine("public:");
            sb.AppendLine($"    const char* GetDataName() override {{ return \"{tableName}\"; }}");
            sb.AppendLine();
            sb.AppendLine("protected:");
            sb.AppendLine("    virtual bool makeGameData(const std::string& line) override;");
            sb.AppendLine();
            sb.AppendLine("protected:");
            sb.AppendLine($"    inline static std::map<int64_t, const GameData_{tableName}*> sm_dataMap;");
            sb.AppendLine("};");

            return sb.ToString();
        }

        private static string BuildBaseCpp(string tableName, List<ColumnInfo> columns)
        {
            var serverCols = columns.Where(c => c.IsForServer).ToList();

            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.");
            sb.AppendLine("// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("#include <sstream>");
            sb.AppendLine("#include <string>");
            sb.AppendLine("#include <format>");
            sb.AppendLine();
            sb.AppendLine($"#include \"GameDataBase_{tableName}.h\"");
            sb.AppendLine($"#include \"GameData_{tableName}.h\"");
            sb.AppendLine();
            sb.AppendLine($"const GameData_{tableName}* GameDataTableBase_{tableName}::FindData(int64_t key)");
            sb.AppendLine("{");
            sb.AppendLine("    auto iter = sm_dataMap.find(key);");
            sb.AppendLine("    if (iter == sm_dataMap.cend())");
            sb.AppendLine("        return nullptr;");
            sb.AppendLine("    return iter->second;");
            sb.AppendLine("}");
            sb.AppendLine();
            sb.AppendLine($"bool GameDataTableBase_{tableName}::makeGameData(const std::string& line)");
            sb.AppendLine("{");
            sb.AppendLine("    std::stringstream ss(line);");
            sb.AppendLine("    std::string field;");
            sb.AppendLine($"    GameData_{tableName}* pData = new GameData_{tableName};");
            sb.AppendLine();
            sb.AppendLine("    // 컬럼 순서대로 파싱");
            foreach (var col in serverCols)
            {
                string parser = ToCppParser(col);
                sb.AppendLine($"    std::getline(ss, field, ','); pData->{col.FieldName} = {parser};");
            }
            sb.AppendLine();
            sb.AppendLine("    if (pData->Key <= 0)");
            sb.AppendLine("    {");
            sb.AppendLine($"        LOG_WRITE(LogLevel::Error, std::format(\"invalid key value. (TableKey = {{}})\", pData->Key));");
            sb.AppendLine("        delete pData;");
            sb.AppendLine("        return false;");
            sb.AppendLine("    }");
            sb.AppendLine();
            sb.AppendLine("    if (sm_dataMap.contains(pData->Key))");
            sb.AppendLine("    {");
            sb.AppendLine($"        LOG_WRITE(LogLevel::Error, std::format(\"Duplicate table key. (TableKey = {{}})\", pData->Key));");
            sb.AppendLine("        delete pData;");
            sb.AppendLine("        return false;");
            sb.AppendLine("    }");
            sb.AppendLine();
            sb.AppendLine("    if (false == pData->Initialize())");
            sb.AppendLine("    {");
            sb.AppendLine($"        LOG_WRITE(LogLevel::Error, std::format(\"Failed to initialize data. (TableKey = {{}})\", pData->Key));");
            sb.AppendLine("        delete pData;");
            sb.AppendLine("        return false;");
            sb.AppendLine("    }");
            sb.AppendLine();
            sb.AppendLine("    sm_dataMap.insert(std::pair(pData->Key, pData));");
            sb.AppendLine();
            sb.AppendLine("    return OnAddData(pData);");
            sb.AppendLine("}");

            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // GameData_Xxx.h + GameData_Xxx.cpp 생성 (파일 없을 때만)
        // ----------------------------------------------------------------
        public static void GenerateDataFiles(string tableName, string outputDir)
        {
            string headerPath = Path.Combine(outputDir, $"GameData_{tableName}.h");
            string cppPath    = Path.Combine(outputDir, $"GameData_{tableName}.cpp");

            if (!File.Exists(headerPath))
                File.WriteAllText(headerPath, BuildDataHeader(tableName), Encoding.UTF8);

            if (!File.Exists(cppPath))
                File.WriteAllText(cppPath, BuildDataCpp(tableName), Encoding.UTF8);
        }

        private static string BuildDataHeader(string tableName)
        {
            var sb = new StringBuilder();
            sb.AppendLine("#pragma once");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.");
            sb.AppendLine("// 이후에는 사용자가 직접 수정할 수 있습니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine($"#include \"GameDataBase_{tableName}.h\"");
            sb.AppendLine();
            sb.AppendLine();
            sb.AppendLine($"// {tableName} 데이터 1건을 표현합니다.");
            sb.AppendLine($"struct GameData_{tableName} : public GameDataBase_{tableName}");
            sb.AppendLine("{");
            sb.AppendLine("public:");
            sb.AppendLine("    bool Initialize();");
            sb.AppendLine();
            sb.AppendLine("    // 여기에 사용자가 추가할 멤버변수, 멤버함수를 선언합니다.");
            sb.AppendLine("};");
            sb.AppendLine();
            sb.AppendLine();
            sb.AppendLine($"// {tableName} 데이터 전체를 관리합니다.");
            sb.AppendLine($"class GameDataTable_{tableName} : public GameDataTableBase_{tableName}");
            sb.AppendLine("{");
            sb.AppendLine("public:");
            sb.AppendLine($"    GameDataTable_{tableName}() = default;");
            sb.AppendLine($"    ~GameDataTable_{tableName}() = default;");
            sb.AppendLine();
            sb.AppendLine("public:");
            sb.AppendLine($"    virtual bool OnAddData(const GameData* pRawData) override;");
            sb.AppendLine($"    virtual bool OnLoadComplete() override;");
            sb.AppendLine();
            sb.AppendLine("    // 여기에 사용자가 추가할 멤버함수를 선언합니다.");
            sb.AppendLine("};");

            return sb.ToString();
        }

        private static string BuildDataCpp(string tableName)
        {
            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.");
            sb.AppendLine("// 이후에는 사용자가 직접 수정할 수 있습니다.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("#include \"LoggerLib.h\"");
            sb.AppendLine($"#include \"GameData_{tableName}.h\"");
            sb.AppendLine();
            sb.AppendLine();
            sb.AppendLine($"bool GameData_{tableName}::Initialize()");
            sb.AppendLine("{");
            sb.AppendLine("    // 데이터 1건을 읽은 직후 호출됨");
            sb.AppendLine();
            sb.AppendLine("    return true;");
            sb.AppendLine("}");
            sb.AppendLine();
            sb.AppendLine($"bool GameDataTable_{tableName}::OnAddData(const GameData* pRawData)");
            sb.AppendLine("{");
            sb.AppendLine($"    const GameData_{tableName}* pData = static_cast<const GameData_{tableName}*>(pRawData);");
            sb.AppendLine();
            sb.AppendLine("    // 데이터가 sm_dataMap 에 추가된 후 호출됨");
            sb.AppendLine();
            sb.AppendLine("    return true;");
            sb.AppendLine("}");
            sb.AppendLine();
            sb.AppendLine($"bool GameDataTable_{tableName}::OnLoadComplete()");
            sb.AppendLine("{");
            sb.AppendLine("    for (const auto& [key, pData] : sm_dataMap)");
            sb.AppendLine("    {");
            sb.AppendLine("        // 여기에 전체 데이터 로드 완료 후 사용자 로직을 작성합니다.");
            sb.AppendLine("        // 예: 다른 테이블과의 참조 연결, 유효성 검증 등");
            sb.AppendLine("    }");
            sb.AppendLine();
            sb.AppendLine("    return true;");
            sb.AppendLine("}");

            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // 헬퍼
        // ----------------------------------------------------------------
        private static string ToCppType(ColumnInfo col)
        {
            return col.DataType switch
            {
                "int"    => "int64_t",
                "string" => "std::string",
                "double" => "double",
                "bool"   => "bool",
                "enum"   => $"E{col.DataTypeDetail}",
                _        => "int64_t"
            };
        }

        private static string ToCppDefaultValue(ColumnInfo col)
        {
            return col.DataType switch
            {
                "int"    => string.IsNullOrEmpty(col.DefaultValue) ? "0" : col.DefaultValue,
                "string" => $"\"{col.DefaultValue}\"",
                "double" => string.IsNullOrEmpty(col.DefaultValue) ? "0.0" : col.DefaultValue,
                "bool"   => col.DefaultValue.ToUpper() == "TRUE" ? "true" : "false",
                "enum"   => $"E{col.DataTypeDetail}::{(string.IsNullOrEmpty(col.DefaultValue) ? "None" : col.DefaultValue)}",
                _        => "0"
            };
        }

        private static string ToCppParser(ColumnInfo col)
        {
            return col.DataType switch
            {
                "int"    => "std::stoll(field)",
                "string" => "field",
                "double" => "std::stod(field)",
                "bool"   => "StringToBool(field)",
                "enum"   => $"static_cast<E{col.DataTypeDetail}>(std::stoi(field))",
                _        => "std::stoll(field)"
            };
        }

        // ----------------------------------------------------------------
        // GameDataManagerBase.cpp 생성 (merge 명령, 항상 덮어씀)
        // ----------------------------------------------------------------
        public static void GenerateManagerFiles(List<string> tableNames, string generatedDir, string managerDir)
        {
            File.WriteAllText(Path.Combine(managerDir, "GameDataManagerBase.cpp"), BuildManagerCpp(tableNames, generatedDir), Encoding.UTF8);
        }

        private static string BuildManagerCpp(List<string> tableNames, string generatedDir)
        {
            var sb = new StringBuilder();
            sb.AppendLine("// =====================================================================");
            sb.AppendLine("// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.");
            sb.AppendLine("// =====================================================================");
            sb.AppendLine();
            sb.AppendLine("#include \"../GameDataManagerBase.h\"");
            sb.AppendLine();
            foreach (var name in tableNames)
                sb.AppendLine($"#include \"../GameData_{name}.h\"");
            sb.AppendLine();
            sb.AppendLine("bool GameDataManagerBase::createAllGameDataTables()");
            sb.AppendLine("{");
            foreach (var name in tableNames)
                sb.AppendLine($"\tif (!createGameDataTable<GameDataTable_{name}>()) return false;");
            sb.AppendLine();
            sb.AppendLine("\treturn true;");
            sb.AppendLine("}");
            return sb.ToString();
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
