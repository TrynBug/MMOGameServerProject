using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using DummyClient;
using DummyClient.Config;
using DummyClient.Nav;
using DummyClient.Sim;

// ── 설정 로드 (인자[0] 또는 실행파일 옆 config.json) ──────────────────────
string configArg = Array.Find(args, a => !a.StartsWith("--"));
string configPath = configArg ?? Path.Combine(AppContext.BaseDirectory, "config.json");

DummyConfig cfg = DummyConfig.Load(configPath);

string navMeshDir = cfg.ResolvePath(cfg.NavMeshDir);
string stageCsv = cfg.ResolvePath(cfg.StageDataCsv);

Console.WriteLine($"[dummy] config    : {configPath}");
Console.WriteLine($"[dummy] login     : {cfg.LoginIp}:{cfg.LoginPort}");
int lastNum = cfg.Account.Start + System.Math.Max(0, cfg.BotCount - 1);
string capNote = cfg.MaxClients > 0 ? $"  [maxClients {cfg.MaxClients}, pool {cfg.PoolSize}]" : "";
Console.WriteLine($"[dummy] bots      : {cfg.BotCount}  ({cfg.Account.Prefix}{cfg.Account.Start}..{cfg.Account.Prefix}{lastNum}){capNote}");
Console.WriteLine($"[dummy] navMeshDir: {navMeshDir}");
Console.WriteLine($"[dummy] stageCsv  : {stageCsv}");

StageCatalog catalog = StageCatalog.Load(stageCsv);

// Skill.csv 는 Stage.csv 와 같은 GameData 폴더에 있다.
string skillCsv = Path.Combine(Path.GetDirectoryName(stageCsv) ?? ".", "Skill.csv");
SkillCatalog skills = SkillCatalog.Load(skillCsv);
Console.WriteLine($"[dummy] skillCsv  : {skillCsv}");

// ── 자가진단: 서버 없이 NavMesh 로드 + 경로탐색 검증 ────────────────────────
if (Array.Exists(args, a => a == "--navtest"))
{
    int stageKey = cfg.StageKeys.Length > 0 ? cfg.StageKeys[0] : 100;
    string name = catalog.GetNavMeshName(stageKey) ?? "SyntyForest";
    Console.WriteLine($"[navtest] stage {stageKey} → navmesh '{name}'");

    var mesh = NavMeshCache.GetMesh(navMeshDir, name);
    if (mesh == null) { Console.WriteLine("[navtest] FAIL: mesh 로드 실패"); return; }

    var nav = new NavField(mesh);
    var a0 = new System.Numerics.Vector3(0, 0, 0);
    var a1 = new System.Numerics.Vector3(40, 0, 40);
    bool s0 = nav.SamplePosition(a0, 30f, out var p0);
    bool s1 = nav.SamplePosition(a1, 30f, out var p1);
    Console.WriteLine($"[navtest] sample origin={s0} {p0}   target={s1} {p1}");

    var path = new System.Collections.Generic.List<System.Numerics.Vector3>();
    bool ok = s0 && s1 && nav.FindPath(p0, p1, path);
    Console.WriteLine($"[navtest] FindPath ok={ok}  waypoints={path.Count}");
    for (int i = 0; i < path.Count && i < 8; i++) Console.WriteLine($"    [{i}] {path[i]}");
    Console.WriteLine(ok && path.Count >= 2 ? "[navtest] PASS" : "[navtest] FAIL");
    return;
}

var manager = new BotManager(cfg, catalog, skills, navMeshDir);

// Ctrl+C 로 전체 종료 (요구사항 7: 모든 클라 연결 끊기)
using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    Console.WriteLine("\n[dummy] 종료 요청 — 모든 봇 연결 종료 중...");
    cts.Cancel();
};

Task runTask = manager.RunAsync(cts.Token);

// ── 현황 출력 루프 ────────────────────────────────────────────────────────
var clock = Stopwatch.StartNew();
while (!cts.IsCancellationRequested)
{
    try { await Task.Delay(cfg.StatusPrintIntervalMs, cts.Token); }
    catch (TaskCanceledException) { break; }
    StatusPrinter.Print(manager.Bots, clock.ElapsedMilliseconds / 1000);
}

await runTask;
Console.WriteLine("[dummy] 종료됨.");
