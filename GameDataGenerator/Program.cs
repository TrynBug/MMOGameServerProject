namespace GameDataGenerator
{
    // ----------------------------------------------------------------
    // 사용법:
    //
    //   enum 생성:
    //     GameDataGenerator.exe enum <GameEnum.xlsx>
    //       --server-code <C++ enum 출력 폴더>
    //       --client-code <C# enum 출력 폴더>
    //
    //   데이터 생성:
    //     GameDataGenerator.exe data <데이터.xlsx>
    //       --enum-xlsx   <GameEnum.xlsx>
    //       --server-code <C++ 코드 출력 폴더>
    //       --server-csv  <서버 csv 출력 폴더>
    //       --client-code <C# 코드 출력 폴더>
    //       --client-csv  <클라 csv 출력 폴더>
    //       [--mode code|csv|all]   (기본값: all)
    //
    //   Manager 파일 생성:
    //     GameDataGenerator.exe merge
    //       --server-code <Generated C++ 폴더>   (GameData_Xxx.cpp 파일 스캔)
    //       --client-code <Generated C# 폴더>    (GameData_Xxx.cs  파일 스캔)
    // ----------------------------------------------------------------
    internal class Program
    {
        static void Main(string[] args)
        {
            if (args.Length < 1)
            {
                PrintUsage();
                return;
            }

            string command = args[0].ToLower();
            var options = ParseOptions(args, startIndex: 1);

            if (command == "enum")
            {
                if (args.Length < 2) { PrintUsage(); return; }
                string xlsxPath = args[1];
                options = ParseOptions(args, startIndex: 2);
                if (!RequireOptions(options, "--server-code", "--client-code")) return;
                RunEnum(xlsxPath, options);
            }
            else if (command == "data")
            {
                if (args.Length < 2) { PrintUsage(); return; }
                string xlsxPath = args[1];
                options = ParseOptions(args, startIndex: 2);
                if (!RequireOptions(options, "--enum-xlsx", "--server-code", "--server-csv", "--client-code", "--client-csv")) return;
                RunData(xlsxPath, options);
            }
            else if (command == "merge")
            {
                if (!RequireOptions(options, "--server-code", "--client-code")) return;
                RunMerge(options);
            }
            else
            {
                PrintUsage();
            }
        }

        // ----------------------------------------------------------------
        // enum 처리
        // ----------------------------------------------------------------
        private static void RunEnum(string xlsxPath, Dictionary<string, string> options)
        {
            string serverCodeDir = options["--server-code"];
            string clientCodeDir = options["--client-code"];

            Console.WriteLine($"[enum] 읽기: {xlsxPath}");
            var enums = XlsxReader.ReadEnumXlsx(xlsxPath);
            Console.WriteLine($"  -> enum {enums.Count}개 로드");

            EnsureDirectories(serverCodeDir, clientCodeDir);

            CppCodeGenerator.GenerateEnumFiles(enums, serverCodeDir);
            CsCodeGenerator.GenerateEnumFiles(enums, clientCodeDir);

            Console.WriteLine($"  -> C++ 출력: {serverCodeDir}");
            Console.WriteLine($"  -> C#  출력: {clientCodeDir}");
            Console.WriteLine("[enum] 완료");
        }

        // ----------------------------------------------------------------
        // data 처리
        // ----------------------------------------------------------------
        private static void RunData(string xlsxPath, Dictionary<string, string> options)
        {
            string enumXlsxPath = options["--enum-xlsx"];
            string serverCodeDir = options["--server-code"];
            string serverCsvDir  = options["--server-csv"];
            string clientCodeDir = options["--client-code"];
            string clientCsvDir  = options["--client-csv"];
            string mode = options.TryGetValue("--mode", out var m) ? m.ToLower() : "all";

            Console.WriteLine($"[data] 읽기: {xlsxPath}  (mode={mode})");

            var allEnums = new List<EnumInfo>();
            var enumValueMap = new Dictionary<string, Dictionary<string, int>>();
            if (File.Exists(enumXlsxPath))
            {
                allEnums = XlsxReader.ReadEnumXlsx(enumXlsxPath);
                enumValueMap = XlsxReader.BuildEnumValueMap(allEnums);
            }
            else
            {
                Console.WriteLine($"  [경고] GameEnum.xlsx 를 찾지 못했습니다: {enumXlsxPath}");
            }

            var (tableName, columns) = XlsxReader.ReadDataXlsx(xlsxPath);
            Console.WriteLine($"  -> 테이블명: {tableName}, 컬럼: {columns.Count}개");

            bool genCode = mode == "code" || mode == "all";
            bool genCsv  = mode == "csv"  || mode == "all";

            if (genCode)
            {
                EnsureDirectories(serverCodeDir, clientCodeDir);

                CppCodeGenerator.GenerateBaseFiles(tableName, columns, allEnums, serverCodeDir);
                CppCodeGenerator.GenerateDataFiles(tableName, serverCodeDir);
                Console.WriteLine($"  -> C++ 출력: {serverCodeDir}");

                CsCodeGenerator.GenerateBaseFile(tableName, columns, clientCodeDir);
                CsCodeGenerator.GenerateDataFile(tableName, clientCodeDir);
                Console.WriteLine($"  -> C#  출력: {clientCodeDir}");
            }

            if (genCsv)
            {
                EnsureDirectories(serverCsvDir, clientCsvDir);

                CsvWriter.Write(xlsxPath, tableName, columns, enumValueMap, serverCsvDir, isServer: true);
                CsvWriter.Write(xlsxPath, tableName, columns, enumValueMap, clientCsvDir, isServer: false);
                Console.WriteLine($"  -> csv 서버: {serverCsvDir}");
                Console.WriteLine($"  -> csv 클라: {clientCsvDir}");
            }

            Console.WriteLine("[data] 완료");
        }

        // ----------------------------------------------------------------
        // merge 처리: Generated 폴더 스캔 → GameDataManagerBase 파일 생성
        // ----------------------------------------------------------------
        private static void RunMerge(Dictionary<string, string> options)
        {
            string serverCodeDir = options["--server-code"];
            string clientCodeDir = options["--client-code"];

            Console.WriteLine("[merge] 시작");

            // Generated 폴더에서 GameData_Xxx.cpp 파일 스캔 → 테이블 이름 추출
            var tableNames = Directory
                .EnumerateFiles(serverCodeDir, "GameData_*.cpp")
                .Select(f => Path.GetFileNameWithoutExtension(f))  // GameData_Monster
                .Select(n => n.Substring("GameData_".Length))       // Monster
                .OrderBy(n => n)
                .ToList();

            Console.WriteLine($"  -> 테이블 {tableNames.Count}개 발견: {string.Join(", ", tableNames)}");

            // C++ Manager 파일 생성
            string serverManagerDir = Path.Combine(serverCodeDir, "Manager");
            EnsureDirectories(serverManagerDir);
            CppCodeGenerator.GenerateManagerFiles(tableNames, serverCodeDir, serverManagerDir);
            Console.WriteLine($"  -> C++ 출력: {serverManagerDir}");

            // C# Manager 파일 생성
            var csTableNames = Directory
                .EnumerateFiles(clientCodeDir, "GameData_*.cs")
                .Select(f => Path.GetFileNameWithoutExtension(f))
                .Select(n => n.Substring("GameData_".Length))
                .OrderBy(n => n)
                .ToList();

            CsCodeGenerator.GenerateManagerFile(csTableNames, clientCodeDir);
            Console.WriteLine($"  -> C#  출력: {clientCodeDir}");

            Console.WriteLine("[merge] 완료");
        }

        // ----------------------------------------------------------------
        // 헬퍼
        // ----------------------------------------------------------------
        private static Dictionary<string, string> ParseOptions(string[] args, int startIndex)
        {
            var options = new Dictionary<string, string>();
            for (int i = startIndex; i < args.Length; i++)
            {
                if (!args[i].StartsWith("--"))
                    continue;
                string key = args[i].ToLower();
                string value = (i + 1 < args.Length && !args[i + 1].StartsWith("--")) ? args[++i] : "";
                options[key] = value;
            }
            return options;
        }

        private static bool RequireOptions(Dictionary<string, string> options, params string[] keys)
        {
            foreach (var key in keys)
            {
                if (!options.ContainsKey(key))
                {
                    Console.WriteLine($"[오류] 필수 인자가 없습니다: {key}");
                    PrintUsage();
                    return false;
                }
            }
            return true;
        }

        private static void EnsureDirectories(params string[] dirs)
        {
            foreach (var dir in dirs)
                Directory.CreateDirectory(dir);
        }

        private static void PrintUsage()
        {
            Console.WriteLine("사용법:");
            Console.WriteLine();
            Console.WriteLine("  enum 생성:");
            Console.WriteLine("    GameDataGenerator.exe enum <GameEnum.xlsx>");
            Console.WriteLine("      --server-code <C++ enum 출력 폴더>");
            Console.WriteLine("      --client-code <C# enum 출력 폴더>");
            Console.WriteLine();
            Console.WriteLine("  데이터 생성:");
            Console.WriteLine("    GameDataGenerator.exe data <데이터.xlsx>");
            Console.WriteLine("      --enum-xlsx   <GameEnum.xlsx>");
            Console.WriteLine("      --server-code <C++ 코드 출력 폴더>");
            Console.WriteLine("      --server-csv  <서버 csv 출력 폴더>");
            Console.WriteLine("      --client-code <C# 코드 출력 폴더>");
            Console.WriteLine("      --client-csv  <클라 csv 출력 폴더>");
            Console.WriteLine("      [--mode code|csv|all]  (기본값: all)");
            Console.WriteLine();
            Console.WriteLine("  Manager 파일 생성:");
            Console.WriteLine("    GameDataGenerator.exe merge");
            Console.WriteLine("      --server-code <Generated C++ 폴더>");
            Console.WriteLine("      --client-code <Generated C# 폴더>");
        }
    }
}
