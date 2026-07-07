using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 효과음 재생기 — 적중음/시전음/발사음 같은 원샷 SFX. 클라 전용, 패킷 0.
    //
    // 스킬데이터가 준 "클립 1개의 정확한 경로"(예: "Sounds/Ultimate Magic Spells/Spell Casts/Fire Spell - Cast 1")를
    // 그대로 로드해 ±피치 변주로 재생한다.
    //  - 같은 클립이라도 ±피치 → 연타해도 기관총처럼 안 뭉개진다(청각 위계).
    //  - AudioSource 풀(고정 개수)을 돌려쓴다. 가득 차면 가장 오래된 소스를 가로채 재사용
    //    → 동시 발성 수가 풀 크기로 캡된다(voice limiting).
    // 싱글톤. 최초 Play 시 자동 생성된다(씬 전환에도 유지). prefab/씬 배치 불필요.
    //
    // v1 단순화: 2D 재생(공간화 없음). 3D 공간화·거리 기반 voice 우선순위는 후속.
    public class SfxPlayer : MonoBehaviour
    {
        private const int k_poolSize = 16;          // 동시 발성 수 = voice cap
        private const float k_pitchVar = 0.08f;     // ±8% 피치 변주
        private const float k_volume = 0.8f;        // 단일 재생 볼륨
        private const float k_coalesceWindowMs = 70f;    // 같은 클립이 이 시간 내 또 울리면 새 보이스 대신 볼륨만 키움
        private const float k_clusterAddVolume = 0.07f;  // 윈도우 내 추가 적중마다 더할 볼륨(무게감)
        private const float k_clusterMaxVolume = 1.0f;   // 뭉친 적중의 최대 볼륨(AudioSource 상한)

        // 거리 감쇠(2D 유지, 로컬 플레이어 기준 XZ 거리). 이 안은 원래 볼륨, 이 밖은 무음, 사이는 선형.
        private const float k_fullVolumeDist = 8f;    // 이 거리 이내는 감쇠 없음(원래 볼륨)
        private const float k_silenceDist    = 35f;   // 이 거리 이상은 무음(0)

        private static SfxPlayer s_inst;

        private AudioSource[] m_pool;
        private int m_nextSource;
        private readonly Dictionary<string, AudioClip> m_clips = new Dictionary<string, AudioClip>();   // 경로 → 클립(Resources.Load 캐시, 실패 시 null 도 캐시)
        private readonly Dictionary<string, Cluster> m_cluster = new Dictionary<string, Cluster>();      // 경로 → 진행 중인 뭉친 타격 상태(시각/소스/볼륨)

        // 같은 클립의 동시(윈도우 내) 적중을 1개 보이스로 묶고 볼륨을 키우기 위한 상태.
        private class Cluster
        {
            public float StartTime;
            public AudioSource Src;
            public float Volume;
        }

        // path = Resources 기준 클립 1개의 정확한 경로(확장자 없음).
        //   예: "Sounds/Ultimate Magic Spells/Target Hits/Target Hit - Fire (1)"
        // 비어있으면(해당 슬롯 사운드 없음) 아무것도 안 한다.
        // volumeScale: 기본 1.0(원래 볼륨). 원격 캐스터의 스킬음처럼 작게 재생하려면 <1 로 넘긴다.
        public static void Play(string path, Vector3 pos, float volumeScale = 1f)
        {
            if (string.IsNullOrEmpty(path))
                return;
            ensureInstance();
            s_inst.play(path, pos, volumeScale);
        }

        // path 의 클립을 durationSec 동안 루프 재생 후 정지한다(지속음 — 예: 운석 낙하).
        // 전용 AudioSource 를 하나 만들어 재생하고 durationSec 후 컴포넌트째 제거(=정지)한다. one-shot 풀과 분리.
        public static void PlayLoop(string path, Vector3 pos, float durationSec, float volumeScale = 1f)
        {
            if (string.IsNullOrEmpty(path) || durationSec <= 0f)
                return;
            ensureInstance();
            s_inst.playLoop(path, pos, durationSec, volumeScale);
        }

        private void playLoop(string path, Vector3 pos, float durationSec, float volumeScale)
        {
            AudioClip clip = getClip(path);
            if (clip == null)
                return;

            AudioSource src = gameObject.AddComponent<AudioSource>();
            src.playOnAwake = false;
            src.spatialBlend = 0f;   // 2D (v1)
            src.loop = true;
            src.clip = clip;
            src.volume = k_volume * Mathf.Clamp01(volumeScale) * distanceScale(pos);
            src.Play();
            Destroy(src, durationSec);   // durationSec 후 컴포넌트 제거 → 재생 정지
        }

        private static void ensureInstance()
        {
            if (s_inst != null)
                return;

            GameObject go = new GameObject("SfxPlayer");
            DontDestroyOnLoad(go);
            s_inst = go.AddComponent<SfxPlayer>();
            s_inst.initPool();
        }

        private void initPool()
        {
            m_pool = new AudioSource[k_poolSize];
            for (int i = 0; i < k_poolSize; i++)
            {
                AudioSource src = gameObject.AddComponent<AudioSource>();
                src.playOnAwake = false;
                src.spatialBlend = 0f;   // 2D (v1)
                m_pool[i] = src;
            }
        }

        private void play(string path, Vector3 pos, float volumeScale)
        {
            AudioClip clip = getClip(path);
            if (clip == null)
                return;

            float vol = k_volume * Mathf.Clamp01(volumeScale) * distanceScale(pos);   // 원격 배수 × 거리 감쇠.
            if (vol <= 0.001f)
                return;   // 사거리 밖 → 재생 안 함(보이스 낭비 방지).
            float now = Time.unscaledTime;

            // 뭉친 타격(대표 1회 + 약간 크게): 같은 클립이 짧은 윈도우 내에 또 적중하면 새 보이스를 만들지 않고
            // 이미 울리는 그 소리의 볼륨만 한 단계 키운다. → 동시 적중음 겹침(위상 간섭/음량 합산)을 피하면서도
            // 많이 맞을수록 한 방이 묵직해진다. (윈도우는 첫 적중 기준으로 고정 — 지속 발사는 새 보이스로 분리.)
            if (m_cluster.TryGetValue(path, out Cluster c)
                && c.Src != null && c.Src.clip == clip
                && (now - c.StartTime) * 1000f < k_coalesceWindowMs)
            {
                c.Volume = Mathf.Min(k_clusterMaxVolume, c.Volume + k_clusterAddVolume);
                c.Src.volume = c.Volume;
                return;
            }

            // 새 재생.
            AudioSource src = m_pool[m_nextSource];   // 라운드로빈 풀 → 가득 차면 가장 오래된 소스 가로채기(voice cap)
            m_nextSource = (m_nextSource + 1) % k_poolSize;

            src.transform.position = pos;
            src.clip = clip;
            src.pitch = 1f + Random.Range(-k_pitchVar, k_pitchVar);
            src.volume = vol;
            src.Play();

            if (c == null)
            {
                c = new Cluster();
                m_cluster[path] = c;
            }
            c.StartTime = now;
            c.Src = src;
            c.Volume = vol;
        }

        // 거리 감쇠 배수(0~1). 로컬 플레이어에서 소리 위치까지 XZ 거리 기준(높이 무시 — 쿼터뷰).
        //   k_fullVolumeDist 이내 = 1.0, k_silenceDist 이상 = 0, 사이는 선형.
        //   로컬 플레이어가 없으면(메뉴/스폰 전 등) 감쇠하지 않는다(1.0).
        // 주의: playLoop 는 생성 시점에 1회만 적용 → 재생 중 플레이어가 이동해도 볼륨이 갱신되진 않는다(v1).
        private float distanceScale(Vector3 pos)
        {
            PlayerCharacter lp = (StageManager.Instance != null) ? StageManager.Instance.LocalPlayer : null;
            if (lp == null)
                return 1f;

            Vector3 d = pos - lp.transform.position;
            d.y = 0f;
            float dist = d.magnitude;
            if (dist <= k_fullVolumeDist) return 1f;
            if (dist >= k_silenceDist)    return 0f;
            return 1f - (dist - k_fullVolumeDist) / (k_silenceDist - k_fullVolumeDist);
        }

        // 경로의 클립을 1회 Resources.Load 후 캐시한다. 없으면 1회 경고하고 null 을 캐시한다.
        private AudioClip getClip(string path)
        {
            if (m_clips.TryGetValue(path, out AudioClip clip))
                return clip;

            clip = Resources.Load<AudioClip>(path);
            if (clip == null)
                Debug.LogWarning($"[SfxPlayer] 클립 없음: '{path}'");

            m_clips[path] = clip;
            return clip;
        }
    }
}
