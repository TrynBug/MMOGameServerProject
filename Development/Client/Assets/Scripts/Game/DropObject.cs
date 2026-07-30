using Client.Network;
using System.Collections;
using UnityEngine;

namespace Client.Game
{
    // 서버가 결정한 착지 위치까지 짧은 포물선 연출을 재생하는 드롭 아이템 표현이다.
    // 실제 습득 가능 위치는 landing이며, 애니메이션 중 transform 위치는 시각 효과일 뿐
    // 서버 판정에는 사용되지 않는다. 착지가 끝난 뒤에만 StageManager가 습득을 요청한다.
    public class DropObject : MonoBehaviour
    {
        private const float k_scatterDurationSec = 0.5f;
        private const float k_arcHeight = 1.2f;

        public long ObjectId { get; private set; }
        public int ItemKey { get; private set; }
        public int Count { get; private set; }
        public bool IsLanded { get; private set; }

        public void Initialize(long objectId, int itemKey, int count, Vector3 origin, Vector3 landing,
            long createdServerTimeMs)
        {
            ObjectId = objectId;
            ItemKey = itemKey;
            Count = count;

            // AOI에 늦게 진입한 클라이언트가 매번 처음부터 튀는 연출을 재생하지 않도록
            // 서버 생성 시각과 동기화된 NetClock으로 이미 지난 연출 시간을 복원한다.
            float elapsedSec = 0f;
            if (NetClock.IsReady)
                elapsedSec = Mathf.Max(0f, (float)(NetClock.EstServerNowMs() - createdServerTimeMs) / 1000f);

            // 생성 후 0.5초 이상 지난 드롭은 이미 착지한 것으로 보고 즉시 최종 위치에 둔다.
            if (elapsedSec >= k_scatterDurationSec)
            {
                transform.position = landing;
                IsLanded = true;
                return;
            }

            StartCoroutine(playScatter(origin, landing, elapsedSec));
        }

        private IEnumerator playScatter(Vector3 origin, Vector3 landing, float elapsedSec)
        {
            float elapsed = elapsedSec;
            while (elapsed < k_scatterDurationSec)
            {
                float t = elapsed / k_scatterDurationSec;
                // 수평 이동은 선형 보간하고 Y축에 sin 곡선을 더해 양 끝이 정확히
                // origin/landing에 닿는 포물선 형태의 '펑 튀는' 연출을 만든다.
                Vector3 pos = Vector3.Lerp(origin, landing, t);
                pos.y += Mathf.Sin(Mathf.PI * t) * k_arcHeight;
                transform.position = pos;

                elapsed += Time.deltaTime;
                yield return null;
            }

            transform.position = landing;
            IsLanded = true;
        }
    }
}
