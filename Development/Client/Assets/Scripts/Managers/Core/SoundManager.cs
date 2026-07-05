using UnityEngine;

namespace Client.Managers
{
    // 마스터 볼륨 하나만 관리하는 사운드 매니저.
    //
    // 책임:
    //   - 마스터 볼륨(0~1) 보관 + AudioListener.volume 에 적용
    //   - PlayerPrefs 로 볼륨 저장/로드 (다음 실행 때 복원)
    //
    // 책임이 아닌 것 (의도적 — v1 단순화):
    //   - BGM/효과음 개별 볼륨 (현재는 마스터 하나만. AudioMixer 미도입)
    //   - 실제 사운드 재생 (효과음은 SfxPlayer 가 담당)
    //
    // AudioListener.volume 은 씬의 모든 AudioSource 출력에 곱해지는 전역 게인이라,
    // SfxPlayer 등 개별 재생기를 수정하지 않고도 마스터 볼륨을 한 곳에서 제어할 수 있다.
    //
    // 사용법:
    //   float v = Managers.Sound.MasterVolume;
    //   Managers.Sound.SetMasterVolume(0.5f);
    public class SoundManager
    {
        private const string k_prefKey = "sound.master";   // PlayerPrefs 키
        private const float k_defaultVolume = 1f;

        private float m_masterVolume = k_defaultVolume;

        // 현재 마스터 볼륨 (0~1). UI 가 슬라이더 초기값으로 읽는다.
        public float MasterVolume => m_masterVolume;

        // 저장된 볼륨을 로드해 즉시 적용. Managers 생성 시 1회 호출.
        public void Init()
        {
            m_masterVolume = Mathf.Clamp01(PlayerPrefs.GetFloat(k_prefKey, k_defaultVolume));
            apply();
        }

        // 마스터 볼륨 설정(0~1). 즉시 적용하고 저장한다.
        public void SetMasterVolume(float volume)
        {
            m_masterVolume = Mathf.Clamp01(volume);
            apply();
            PlayerPrefs.SetFloat(k_prefKey, m_masterVolume);
            PlayerPrefs.Save();
        }

        private void apply()
        {
            AudioListener.volume = m_masterVolume;
        }
    }
}
