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
        public static void Play(string path, Vector3 pos)
        {
            if (string.IsNullOrEmpty(path))
                return;
            ensureInstance();
            s_inst.play(path, pos);
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

        private void play(string path, Vector3 pos)
        {
            AudioClip clip = getClip(path);
            if (clip == null)
                return;

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
            src.volume = k_volume;
            src.Play();

            if (c == null)
            {
                c = new Cluster();
                m_cluster[path] = c;
            }
            c.StartTime = now;
            c.Src = src;
            c.Volume = k_volume;
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
