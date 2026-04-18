#include "PlayersInfo.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include <nlohmann/json.hpp>

#include <ctime>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

PlayersInfo g_PlayersInfo;
PLUGIN_EXPOSE(PlayersInfo, g_PlayersInfo);

IVEngineServer2 *engine = nullptr;
CGameEntitySystem *g_pGameEntitySystem = nullptr;
CEntitySystem *g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IUtilsApi *g_pUtils = nullptr;
IPlayersApi *g_pPlayers = nullptr;
ILRApi *g_pLRCore = nullptr;

namespace {

constexpr int kMaxPlayers = 64;
constexpr int kTeamCT = 3;
constexpr int kTeamT = 2;
constexpr AppId_t kAppPrime = 624820;
constexpr AppId_t kAppPrimeLegacy = 54029;
constexpr size_t kConsoleChunk = 1024;

time_t g_ConnectionTime[kMaxPlayers] = {};
std::unordered_map<uint64, bool> g_PrimeCache;

bool IsValidSlot(int slot) noexcept {
  return slot >= 0 && slot < kMaxPlayers;
}

bool IsRealPlayer(int slot) {
  return g_pPlayers && g_pPlayers->IsConnected(slot) &&
         !g_pPlayers->IsFakeClient(slot);
}

bool CheckPrime(uint64 steamid64) {
  if (steamid64 == 0)
    return false;

  auto it = g_PrimeCache.find(steamid64);
  if (it != g_PrimeCache.end())
    return it->second;

  ISteamGameServer *pServer = SteamGameServer();
  if (!pServer)
    return false;

  CSteamID id(steamid64);
  const bool prime = pServer->UserHasLicenseForApp(id, kAppPrime) == 0 ||
                     pServer->UserHasLicenseForApp(id, kAppPrimeLegacy) == 0;
  g_PrimeCache.emplace(steamid64, prime);
  return prime;
}

void ConPrintChunked(const std::string &str) {
  const size_t n = str.size();
  for (size_t i = 0; i < n; i += kConsoleChunk)
    META_CONPRINT(str.substr(i, kConsoleChunk).c_str());
}

struct TeamScores {
  int ct = 0;
  int t = 0;
};

TeamScores GetTeamScores() {
  TeamScores scores;
  std::vector<CEntityInstance *> teams =
      UTIL_FindEntityByClassnameAll("cs_team_manager");
  for (CEntityInstance *inst : teams) {
    auto *pTeam = static_cast<CTeam *>(inst);
    if (!pTeam)
      continue;

    const int num = pTeam->m_iTeamNum();
    if (num == kTeamCT)
      scores.ct = pTeam->m_iScore();
    else if (num == kTeamT)
      scores.t = pTeam->m_iScore();
  }
  return scores;
}

std::string GetMapNameSafe() {
  if (!gpGlobals)
    return std::string();
  char buf[64] = {};
  g_SMAPI->Format(buf, sizeof(buf), "%s", gpGlobals->mapname);
  return std::string(buf);
}

json BuildPlayerJson(int slot, CCSPlayerController *ctrl, time_t now) {
  json j;

  const char *name = g_pPlayers->GetPlayerName(slot);
  const uint64 steamid64 = g_pPlayers->GetSteamID64(slot);

  j["userid"] = slot;
  j["name"] = (name && name[0] != '\0') ? name : "Unknown";
  j["team"] = ctrl->GetTeam();
  j["steamid"] = std::to_string(steamid64);

  int kills = 0, deaths = 0, headshots = 0;
  if (auto *ats = ctrl->m_pActionTrackingServices()) {
    kills = ats->m_matchStats().m_iKills();
    deaths = ats->m_matchStats().m_iDeaths();
    headshots = ats->m_matchStats().m_iHeadShotKills();
  }
  j["kills"] = kills;
  j["death"] = deaths;
  j["headshots"] = headshots;

  j["ping"] = ctrl->m_iPing();
  j["playtime"] = static_cast<int64_t>(now - g_ConnectionTime[slot]);
  j["prime"] = CheckPrime(steamid64);
  j["alive"] = (ctrl->m_hPlayerPawn() != nullptr);

  if (g_pLRCore && g_pLRCore->GetClientStatus(slot))
    j["rank"] = g_pLRCore->GetClientInfo(slot, ST_RANK);

  return j;
}

json GetServerInfo() {
  const time_t now = std::time(nullptr);

  json jdata;
  jdata["time"] = now;
  jdata["current_map"] = GetMapNameSafe();

  const TeamScores scores = GetTeamScores();
  jdata["score_ct"] = scores.ct;
  jdata["score_t"] = scores.t;

  json players = json::array();
  if (g_pPlayers) {
    for (int i = 0; i < kMaxPlayers; ++i) {
      if (g_ConnectionTime[i] == 0)
        continue;

      if (!IsRealPlayer(i)) {
        g_ConnectionTime[i] = 0;
        continue;
      }

      CCSPlayerController *ctrl = CCSPlayerController::FromSlot(i);
      if (!ctrl)
        continue;

      players.push_back(BuildPlayerJson(i, ctrl, now));
    }
  }

  jdata["player_count"] = players.size();
  jdata["players"] = std::move(players);
  return jdata;
}

}  // namespace

CON_COMMAND_F(mm_getinfo, "", FCVAR_GAMEDLL) {
  const std::string dump = GetServerInfo().dump();
  ConPrintChunked(dump);
  META_CONPRINT("\n");
}

static void OnStartupServer() {
  g_pGameEntitySystem = g_pUtils->GetCGameEntitySystem();
  g_pEntitySystem = g_pUtils->GetCEntitySystem();
  gpGlobals = g_pUtils->GetCGlobalVars();

  g_PrimeCache.clear();

  const time_t now = std::time(nullptr);
  for (int i = 0; i < kMaxPlayers; ++i)
    g_ConnectionTime[i] = IsRealPlayer(i) ? now : 0;
}

static void OnPlayerConnect(const char *, IGameEvent *pEvent, bool) {
  const int slot = pEvent->GetInt("userid");
  if (!IsValidSlot(slot))
    return;
  if (g_pPlayers && g_pPlayers->IsFakeClient(slot))
    return;
  g_ConnectionTime[slot] = std::time(nullptr);
}

static void OnPlayerDisconnect(const char *, IGameEvent *pEvent, bool) {
  const int slot = pEvent->GetInt("userid");
  if (!IsValidSlot(slot))
    return;
  g_ConnectionTime[slot] = 0;
}

bool PlayersInfo::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen,
                       bool late) {
  PLUGIN_SAVEVARS();

  GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem,
                  SCHEMASYSTEM_INTERFACE_VERSION);
  GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2,
                      SOURCE2ENGINETOSERVER_INTERFACE_VERSION);

  g_SMAPI->AddListener(this, this);
  ConVar_Register(FCVAR_RELEASE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);

  return true;
}

bool PlayersInfo::Unload(char *error, size_t maxlen) {
  ConVar_Unregister();
  g_PrimeCache.clear();
  for (int i = 0; i < kMaxPlayers; ++i)
    g_ConnectionTime[i] = 0;
  return true;
}

void PlayersInfo::AllPluginsLoaded() {
  int ret = 0;

  g_pUtils = static_cast<IUtilsApi *>(
      g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, nullptr));
  if (ret == META_IFACE_FAILED) {
    ConColorMsg(Color(255, 0, 0, 255), "[%s] Missing Utils system plugin\n",
                GetLogTag());
    engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
    return;
  }

  g_pPlayers = static_cast<IPlayersApi *>(
      g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, nullptr));
  if (ret == META_IFACE_FAILED) {
    g_pUtils->ErrorLog("[%s] Missing Players system plugin", GetLogTag());
    engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
    return;
  }

  g_pLRCore = static_cast<ILRApi *>(
      g_SMAPI->MetaFactory(LR_INTERFACE, &ret, nullptr));
  if (ret == META_IFACE_FAILED)
    g_pLRCore = nullptr;

  g_pUtils->StartupServer(g_PLID, OnStartupServer);
  g_pUtils->HookEvent(g_PLID, "player_connect", OnPlayerConnect);
  g_pUtils->HookEvent(g_PLID, "player_disconnect", OnPlayerDisconnect);
}

const char *PlayersInfo::GetLicense() { return "GPL"; }
const char *PlayersInfo::GetVersion() { return "1.1.0"; }
const char *PlayersInfo::GetDate() { return __DATE__; }
const char *PlayersInfo::GetLogTag() { return "PlayersInfo"; }
const char *PlayersInfo::GetAuthor() { return "Pisex"; }
const char *PlayersInfo::GetDescription() { return "PlayersInfo"; }
const char *PlayersInfo::GetName() { return "PlayersInfo"; }
const char *PlayersInfo::GetURL() { return "https://discord.gg/g798xERK5Y"; }
