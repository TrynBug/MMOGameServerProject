using System;
using System.Collections.Generic;
using System.Text;
using DummyClient.Sim;

namespace DummyClient
{
    // 봇 현황을 콘솔에 주기 출력. 상태별 카운트 + 총 송/수신 패킷.
    public static class StatusPrinter
    {
        public static void Print(IReadOnlyList<Bot> bots, long uptimeSec)
        {
            var counts = new int[Enum.GetValues(typeof(BotState)).Length];
            int totalSent = 0, totalRecv = 0, totalSkills = 0, totalMoves = 0, fighting = 0;
            var stageDist = new Dictionary<int, int>();
            string firstError = null;

            foreach (var b in bots)
            {
                counts[(int)b.State]++;
                totalSent += b.PacketsSent;
                totalRecv += b.PacketsRecv;
                totalSkills += b.SkillsCast;
                totalMoves += b.StageMoves;
                if (b.IsFighting) fighting++;
                if (b.State == BotState.InStage)
                    stageDist[b.StageDataKey] = stageDist.GetValueOrDefault(b.StageDataKey) + 1;
                if (firstError == null && b.LastError != null)
                    firstError = $"[{b.LoginId}] {b.LastError}";
            }

            var sb = new StringBuilder();
            sb.AppendLine("──────────────────────────────────────────────");
            sb.AppendLine($" DummyClient  |  uptime {uptimeSec,5}s  |  bots {bots.Count}");
            sb.AppendLine("──────────────────────────────────────────────");
            sb.AppendLine($"  InStage    : {counts[(int)BotState.InStage],5}   (fighting {fighting}, roaming {counts[(int)BotState.InStage] - fighting})");
            sb.AppendLine($"  Connecting : {counts[(int)BotState.ConnectingLogin] + counts[(int)BotState.ConnectingGateway] + counts[(int)BotState.NeedGatewayConnect],5}");
            sb.AppendLine($"  Handshake  : {counts[(int)BotState.WaitLoginRes] + counts[(int)BotState.WaitCharList] + counts[(int)BotState.WaitCreate] + counts[(int)BotState.WaitSelect] + counts[(int)BotState.WaitStageLoad],5}");
            sb.AppendLine($"  StageMoving: {counts[(int)BotState.WaitStageMoveRes],5}");
            sb.AppendLine($"  Idle       : {counts[(int)BotState.Idle],5}");
            sb.AppendLine($"  Disconnect : {counts[(int)BotState.Disconnected],5}");
            sb.AppendLine($"  Packets    : sent {totalSent}  recv {totalRecv}");
            sb.AppendLine($"  Activity   : skills {totalSkills}  stageMoves {totalMoves}");

            if (stageDist.Count > 0)
            {
                sb.Append("  Stages     :");
                foreach (var kv in stageDist)
                    sb.Append($"  [{kv.Key}]={kv.Value}");
                sb.AppendLine();
            }
            if (firstError != null)
                sb.AppendLine($"  lastError  : {firstError}");

            try { Console.Clear(); }
            catch { /* 출력 리다이렉트 등 콘솔 없음 → 클리어 생략 */ }
            Console.Write(sb.ToString());
        }
    }
}
