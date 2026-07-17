using System.Collections.Concurrent;
using System.IO;
using DotRecast.Detour;
using DotRecast.Detour.Io;

namespace DummyClient.Nav
{
    // .bin NavMesh 를 파일당 1번만 로드해 DtNavMesh(불변)를 봇들 사이에서 공유한다.
    public static class NavMeshCache
    {
        private static readonly ConcurrentDictionary<string, DtNavMesh> s_meshes = new();

        // navMeshDir 안의 <fileName>.bin 을 로드. 실패 시 null.
        public static DtNavMesh GetMesh(string navMeshDir, string fileName)
        {
            string binPath = Path.Combine(navMeshDir, fileName + ".bin");
            return s_meshes.GetOrAdd(binPath, LoadBin);
        }

        private static DtNavMesh LoadBin(string binPath)
        {
            if (!File.Exists(binPath))
            {
                System.Console.WriteLine($"[navmesh] .bin 없음: {binPath}");
                return null;
            }
            try
            {
                var reader = new DtMeshSetReader();
                using var fs = File.OpenRead(binPath);
                using var br = new BinaryReader(fs);
                // 클라와 동일: 64bit tileRef, maxVertPerPoly=6
                return reader.Read(br, 6);
            }
            catch (System.Exception e)
            {
                System.Console.WriteLine($"[navmesh] 로드 실패 {binPath}: {e.Message}");
                return null;
            }
        }
    }
}
