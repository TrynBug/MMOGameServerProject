using Client.Managers;
using Client.Network;
using Client.Packet;
using Client.UI;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // Game 씬의 BaseScene 구현.
    //
    // Game 씬에 빈 GameObject "@Scene" 을 만들고 이 컴포넌트를 부착하면,
    // Start 시점에 Init() 이 호출된다.
    //
    // 책임:
    //   - 캐시의 LocalCharacter 데이터모델로 LocalPlayer GameObject 1회 생성(비활성 상태)
    //   - StageManager 에 스테이지 로딩 시작 요청 (BeginStageLoad)
    //     → 로딩 완료 보고(StageLoadCompleteReq) → 서버가 스폰 → StageLoadCompleteRes 로 LocalPlayer 활성화/배치
    //
    // 책임이 아닌 것:
    //   - 게임 로직 (StageManager 가 함)
    //   - 캐릭터 GameObject 생성 자체 (CharacterFactory 가 함)
    //   - 패킷 핸들링 (StageManager 가 함)
    public class GameScene : BaseScene
    {
        protected override void Init()
        {
            Debug.Log("[GameScene] Init");

            // 캐시에서 내 캐릭터 데이터모델 꺼내기. 정상 흐름이면 CharacterSelector 가 채워놨음.
            DataStructures.Character localData = CharacterDataCache.Instance.LocalCharacter;
            if (localData == null)
            {
                Debug.LogError("[GameScene] LocalCharacter is null. CharacterSelection 씬을 거치지 않고 진입한 것 같습니다.");
                return;
            }

            if (StageManager.Instance == null)
            {
                Debug.LogError("[GameScene] StageManager.Instance is null. Game 씬에 StageManager 를 배치했는지 확인하세요.");
                return;
            }

            // 데이터모델로 LocalPlayer 를 1회 생성한다 (비활성/조작불가 상태로 보관).
            // 이후 스테이지를 이동해도 파괴하지 않고 숨김+재배치만 한다.
            StageManager.Instance.EnsureLocalPlayer(localData);

            // 스테이지 로딩 시작 (NavMesh 교체 → StageLoadCompleteReq 송신).
            // LocalPlayer 는 이후 StageLoadCompleteRes 수신 시 활성화+배치된다.
            StageManager.Instance.BeginStageLoad(CharacterDataCache.Instance.SelectedStageDataKey);

            // 플레이어 HUD (HP/MP 바 + 스킬 슬롯) + 버프 바 표시.
            Managers.Managers.UI.ShowSceneUI<UI_PlayerHud>();
            Managers.Managers.UI.ShowSceneUI<UI_BuffBar>();

            // Esc 메뉴 토글 구독.
            Managers.Managers.Input.OnMenu += toggleMenu;
        }

        public override void Clear()
        {
            Debug.Log("[GameScene] Clear");
            Managers.Managers.Input.OnMenu -= toggleMenu;
            Managers.Managers.Input.SetGameplayBlocked(false);   // 씬 떠날 때 입력 차단 해제(안전장치)
            // 추후: BGM 정지, HUD 닫기, 캐릭터 정리 등
        }

        // ─── Esc 메뉴 ──────────────────────────────────────────────────
        private UI_GameMenu m_menu;

        private void toggleMenu()
        {
            if (m_menu != null) closeMenu();
            else openMenu();
        }

        private void openMenu()
        {
            m_menu = Managers.Managers.UI.ShowPopupUI<UI_GameMenu>();
            if (m_menu == null) return;
            m_menu.OnResume += onResume;
            m_menu.OnCharacterSelect += onCharacterSelect;
            m_menu.OnLogout += onLogout;

            // 메뉴가 떠 있는 동안 게임플레이 입력(이동/스킬)만 차단. Esc(Menu)는 살아있어 다시 닫을 수 있다.
            Managers.Managers.Input.SetGameplayBlocked(true);
        }

        private void closeMenu()
        {
            if (m_menu == null) return;
            m_menu.OnResume -= onResume;
            m_menu.OnCharacterSelect -= onCharacterSelect;
            m_menu.OnLogout -= onLogout;
            Managers.Managers.UI.ClosePopupUI(m_menu);
            m_menu = null;

            // 게임플레이 입력 복구.
            Managers.Managers.Input.SetGameplayBlocked(false);
        }

        private void onResume() => closeMenu();

        // 로그인으로 돌아가기: 연결 끊고 캐시 비우고 Login 씬으로.
        private void onLogout()
        {
            closeMenu();
            NetworkManager.Instance?.Disconnect();
            CharacterDataCache.Instance.Clear();
            Managers.Managers.Scene.LoadScene(SceneManagerEx.SceneNames.Login);
        }

        // 캐릭터 선택으로 돌아가기: 서버에 복귀 요청 (Phase 2 에서 패킷 송신 구현).
        private void onCharacterSelect()
        {
            closeMenu();
            requestReturnToCharacterSelect();
        }

        // 캐릭터 선택 화면으로 복귀 요청.
        // 서버가 현재 Stage 에서 퇴장 → SystemStage 복귀 후 CharacterListNtf 를 재전송한다.
        // 그 CharacterListNtf 를 받으면 캐시를 갱신하고 CharacterSelection 씬으로 전환한다.
        private void requestReturnToCharacterSelect()
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
            {
                Debug.LogWarning("[GameScene] 캐릭터 선택 복귀 실패: 연결 없음.");
                return;
            }

            // 응답으로 올 CharacterListNtf 를 1회 처리 (수신 시 씬 전환).
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onReturnCharacterList);
            net.Send(GamePacketId.ReturnToCharacterSelectReq, new ReturnToCharacterSelectReq());
        }

        private void onReturnCharacterList(CharacterListNtf ntf)
        {
            CharacterDataCache.Instance.SetCharacters(ntf.Characters);
            Managers.Managers.Scene.LoadScene(SceneManagerEx.SceneNames.CharacterSelection);
        }
    }
}
