/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <list>
#include <map>
#include <vector>

#include "filesystem/File.h"
#include "FileItem.h"
#include "GUIBaseContainer.h"
#include "GUIStaticItem.h"
#include "GUIWindowHome.h"
#include "GUI/GUIDialogTimer.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIWindowManager.h"
#include "GUIUserMessages.h"
#include "MediaSource.h"
#include "AlarmClock.h"
#include "Key.h"

#include "Client/MyPlex/MyPlexManager.h"
#include "PlexDirectory.h"
#include "threads/SingleLock.h"
#include "PlexUtils.h"
#include "video/VideoInfoTag.h"

#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogVideoInfo.h"
#include "dialogs/GUIDialogOK.h"

#include "Client/PlexMediaServerClient.h"

#include "powermanagement/PowerManager.h"

#include "ApplicationMessenger.h"

#include "AdvancedSettings.h"

#include "Job.h"
#include "JobManager.h"

#include "interfaces/Builtins.h"

#include "Client/PlexServerManager.h"
#include "Client/PlexServerDataLoader.h"
#include "PlexJobs.h"
#include "PlexApplication.h"

#include "ApplicationMessenger.h"

#include "AutoUpdate/PlexAutoUpdate.h"

#include "PlexThemeMusicPlayer.h"
#include "dialogs/GUIDialogBusy.h"
#include "DirectoryCache.h"
#include "GUI/GUIPlexMediaWindow.h"
#include "PlayListPlayer.h"
#include "playlists/PlayList.h"
#include "PlexPlayQueueManager.h"
#include "music/tags/MusicInfoTag.h"
#include "settings/GUISettings.h"

using namespace std;
using namespace XFILE;
using namespace boost;

#define MAIN_MENU         300 // THIS WAS 300 for Plex skin.
#define POWER_MENU        407

#define QUIT_ITEM          111
#define SLEEP_ITEM         112
#define SHUTDOWN_ITEM      113
#define SLEEP_DISPLAY_ITEM 114

#define CHANNELS_VIDEO 1
#define CHANNELS_MUSIC 2
#define CHANNELS_PHOTO 3
#define CHANNELS_APPLICATION 4

#define SLIDESHOW_MULTIIMAGE 10101

typedef std::pair<CStdString, CPlexSectionFanout*> nameSectionPair;

//////////////////////////////////////////////////////////////////////////////
CGUIWindowHome::CGUIWindowHome(void) : CGUIWindow(WINDOW_HOME, "Home.xml"), m_globalArt(false), m_lastSelectedItem("Search")
{
  m_loadType = LOAD_ON_GUI_INIT;
  AddSection("global://art/", CPlexSectionFanout::SECTION_TYPE_GLOBAL_FANART, true);
}

//////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::OnAction(const CAction &action)
{
  /* > 9000 is the fans and 506 is preferences */
  if ((action.GetID() == ACTION_PREVIOUS_MENU || action.GetID() == ACTION_NAV_BACK) &&
      (GetFocusedControlID() > 9000 || GetFocusedControlID() == 506))
  {
    CGUIMessage msg(GUI_MSG_SETFOCUS, GetID(), 300);
    OnMessage(msg);

    return true;
  }
  
  if (action.GetID() == ACTION_CONTEXT_MENU)
  {
    return OnPopupMenu();
  }
  else if (action.GetID() == ACTION_PREVIOUS_MENU || action.GetID() == ACTION_PARENT_DIR)
  {
    CGUIMessage msg(GUI_MSG_SETFOCUS, GetID(), 300);
    OnMessage(msg);
    
    return true;
  }
  
  bool ret = CGUIWindow::OnAction(action);
  
  int focusedControl = GetFocusedControlID();

  // See what's focused.
  if (focusedControl == MAIN_MENU)
  {
    CFileItemPtr pItem = GetCurrentListItem();
    if (pItem)
    {
      CLog::Log(LOGDEBUG, "CGUIWindowHome::OnAction %s=>%s", pItem->GetLabel().c_str(), pItem->GetProperty("sectionPath").asString().c_str());
      if (m_lastSelectedItem != GetCurrentItemName())
      {
        HideAllLists();
        m_lastSelectedItem = GetCurrentItemName();
        m_lastSelectedSubItem.Empty();
        if (g_plexApplication.timer)
          g_plexApplication.timer->SetTimeout(200, this);
      }

      if (action.GetID() == ACTION_SELECT_ITEM && pItem->HasProperty("sectionPath") &&
          !pItem->GetProperty("navigateDirectly").asBoolean())
      {
        OpenItem(pItem);
        return true;
      }
    }
  }
  else if (focusedControl == CONTENT_LIST_ON_DECK ||
           focusedControl == CONTENT_LIST_RECENTLY_ADDED ||
           focusedControl == CONTENT_LIST_QUEUE ||
           focusedControl == CONTENT_LIST_RECOMMENDATIONS ||
           focusedControl == CONTENT_LIST_RECENTLY_ACCESSED)
  {
    CGUIBaseContainer* pControl = (CGUIBaseContainer*)GetFocusedControl();
    if (pControl)
    {
      CGUIListItemPtr pItem = pControl->GetListItem(0);
      if (pItem)
      {
        m_lastSelectedSubItem = pItem->GetProperty("key").asString();
      }
    }
  }

  
  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CFileItemPtr CGUIWindowHome::GetCurrentListItem(int offset)
{
  CGUIBaseContainer* pControl = (CGUIBaseContainer* )GetControl(MAIN_MENU);
  if (pControl)
  {
    CGUIListItemPtr guiItem = pControl->GetListItem(offset);
    if (guiItem && guiItem->IsFileItem())
      return boost::static_pointer_cast<CFileItem>(guiItem);
  }

  return CFileItemPtr();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CFileItem* CGUIWindowHome::GetCurrentFileItem()
{
  CFileItemPtr listItem = GetCurrentListItem();
  if (listItem && listItem->IsFileItem())
    return (CFileItem*)listItem.get();
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CFileItemPtr CGUIWindowHome::GetCurrentFanoutItem()
{
  int focusedControl = GetFocusedControlID();
  if (focusedControl >= CONTENT_LIST_RECENTLY_ADDED &&
      focusedControl <= CONTENT_LIST_PLAYQUEUE_PHOTO)
  {
    CGUIBaseContainer* container = (CGUIBaseContainer*)(GetControl(focusedControl));
    if (container)
    {
      CGUIListItemPtr listItem = container->GetListItem(0);
      if (listItem && listItem->IsFileItem())
        return boost::static_pointer_cast<CFileItem>(listItem);
    }
  }
  return CFileItemPtr();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::GetSleepContextMenu(CContextButtons& buttons)
{
  buttons.Add(CONTEXT_BUTTON_QUIT, 13009);

  if(g_powerManager.CanSuspend())
    buttons.Add(CONTEXT_BUTTON_SLEEP, 13011);

  if (g_powerManager.CanPowerdown())
    buttons.Add(CONTEXT_BUTTON_SHUTDOWN, 13005);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::GetItemContextMenu(CContextButtons& buttons, const CFileItem& item)
{
  if ((item.GetProperty("HasWatchedState").asBoolean() ||
      item.HasProperty("ratingKey")) && item.HasVideoInfoTag())
  {
    CStdString viewOffset = item.GetProperty("viewOffset").asString();

    if (item.GetVideoInfoTag()->m_playCount > 0 || viewOffset.size() > 0)
      buttons.Add(CONTEXT_BUTTON_MARK_UNWATCHED, 16104);
    if (item.GetVideoInfoTag()->m_playCount == 0 || viewOffset.size() > 0)
      buttons.Add(CONTEXT_BUTTON_MARK_WATCHED, 16103);
  }

  CFileItemList pqlist;
  g_plexApplication.playQueueManager->getCurrentPlayQueue(pqlist);

  if (pqlist.Size())
    buttons.Add(CONTEXT_BUTTON_PLAY_ONLY_THIS, 52602);
  else
    buttons.Add(CONTEXT_BUTTON_PLAY_ONLY_THIS, 52607);

  ePlexMediaType itemType = PlexUtils::GetMediaTypeFromItem(item);
  if (g_plexApplication.playQueueManager->getCurrentPlayQueueType() == itemType)
    buttons.Add(CONTEXT_BUTTON_QUEUE_ITEM, 52603);

  CPlexServerPtr server = g_plexApplication.serverManager->FindByUUID(item.GetProperty("plexserver").asString());
  if (server && server->SupportsDeletion())
    buttons.Add(CONTEXT_BUTTON_DELETE, 117);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::GetPlayQueueContextMenu(CContextButtons& buttons, const CFileItem& item)
{
  // Remove from Queue
  buttons.Add(CONTEXT_BUTTON_REMOVE_SOURCE, 1210);
  buttons.Add(CONTEXT_BUTTON_CLEAR, 192);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::GetContextMenu(CContextButtons& buttons)
{
  if (!GetFocusedControl())
    return;

  int controlId = GetFocusedControl()->GetID();
  CFileItemPtr fileItem = GetCurrentFanoutItem();

  if (controlId == MAIN_MENU || controlId == POWER_MENU)
  {
    GetSleepContextMenu(buttons);
  }
  else if (fileItem != NULL)
  {

    if (PlexUtils::IsPlayingPlaylist())
      buttons.Add(CONTEXT_BUTTON_NOW_PLAYING, 13350);

    if (controlId == CONTENT_LIST_ON_DECK ||
        controlId == CONTENT_LIST_RECENTLY_ADDED ||
        controlId == CONTENT_LIST_QUEUE ||
        controlId == CONTENT_LIST_RECOMMENDATIONS)
      GetItemContextMenu(buttons, *fileItem);

    else if (controlId == CONTENT_LIST_PLAYQUEUE_MUSIC ||
             controlId == CONTENT_LIST_PLAYQUEUE_PHOTO ||
             controlId == CONTENT_LIST_PLAYQUEUE_VIDEO)
      GetPlayQueueContextMenu(buttons, *fileItem);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::ChangeWatchState(int choice)
{
  CFileItemPtr fileItem = GetCurrentFanoutItem();
  if (!fileItem)
    return;

  int controlId = GetFocusedControl()->GetID();
  CGUIBaseContainer *container = (CGUIBaseContainer*)GetControl(controlId);
  std::vector<CGUIListItemPtr> items = container->GetItems();

  int indexInContainer = std::distance(items.begin(),
                                       std::find(items.begin(), items.end(), fileItem));

  CGUIMessage msg(GUI_MSG_LIST_REMOVE_ITEM, GetID(), controlId, indexInContainer+1, 0);

  if (choice == CONTEXT_BUTTON_MARK_UNWATCHED)
  {
    bool sendMsg = false;

    if (controlId == CONTENT_LIST_ON_DECK &&
        fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_MOVIE)
    {
      OnMessage(msg);
    }
    else if (controlId == CONTENT_LIST_ON_DECK &&
             fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_EPISODE)
    {
      SectionNeedsRefresh(GetCurrentItemName());
      sendMsg = true;
    }

    fileItem->MarkAsUnWatched(sendMsg);
  }
  else if (choice == CONTEXT_BUTTON_MARK_WATCHED)
  {
    bool sendMsg = false;
    if (controlId == CONTENT_LIST_RECENTLY_ADDED ||
        (controlId == CONTENT_LIST_ON_DECK &&
         fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_MOVIE))
    {
      OnMessage(msg);
    }

    else if (controlId == CONTENT_LIST_ON_DECK &&
             fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_EPISODE)
    {
      SectionNeedsRefresh(GetCurrentItemName());
      sendMsg = true;
    }

    fileItem->MarkAsWatched(sendMsg);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::HandleItemDelete()
{
  CFileItemPtr fileItem = GetCurrentFanoutItem();
  if (!fileItem)
    return;

  // Confirm.
  if (!CGUIDialogYesNo::ShowAndGetInput(750, 125, 0, 0))
    return;

  g_plexApplication.mediaServerClient->deleteItem(fileItem);

  /* marking as watched and is on the on deck list, we need to remove it then */
  CGUIBaseContainer *container = (CGUIBaseContainer*)GetFocusedControl();
  if (container)
  {
    std::vector<CGUIListItemPtr> items = container->GetItems();
    int idx = std::distance(items.begin(), std::find(items.begin(), items.end(), fileItem));
    CGUIMessage msg(GUI_MSG_LIST_REMOVE_ITEM, GetID(), GetFocusedControlID(), idx+1, 0);
    OnMessage(msg);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::RemoveFromPlayQueue()
{
  CFileItemPtr fileItem = GetCurrentFanoutItem();
  if (fileItem)
    g_plexApplication.playQueueManager->removeItem(fileItem);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::OnPopupMenu()
{

  CContextButtons buttons;
  GetContextMenu(buttons);

  int choice = CGUIDialogContextMenu::ShowAndGetChoice(buttons);

  if (choice == -1)
    return false;

  switch (choice)
  {
    case CONTEXT_BUTTON_SLEEP:
      CApplicationMessenger::Get().Suspend();
      break;
    case CONTEXT_BUTTON_QUIT:
      CApplicationMessenger::Get().Quit();
      break;
    case CONTEXT_BUTTON_SHUTDOWN:
      CApplicationMessenger::Get().Shutdown();
      break;

    case CONTEXT_BUTTON_MARK_UNWATCHED:
    case CONTEXT_BUTTON_MARK_WATCHED:
      ChangeWatchState(choice);
      break;

    case CONTEXT_BUTTON_DELETE:
      HandleItemDelete();
      break;

    case CONTEXT_BUTTON_REMOVE_SOURCE:
      RemoveFromPlayQueue();
      break;

    case CONTEXT_BUTTON_NOW_PLAYING:
      m_navHelper.navigateToNowPlaying();
      break;

    case CONTEXT_BUTTON_CLEAR:
      g_plexApplication.playQueueManager->clear();
      UpdateSections();
      break;

    case CONTEXT_BUTTON_QUEUE_ITEM:
    case CONTEXT_BUTTON_PLAY_ONLY_THIS:
    {
      CFileItemPtr item = GetCurrentFanoutItem();
      if (item)
      {
        g_plexApplication.playQueueManager->QueueItem(item, choice == CONTEXT_BUTTON_PLAY_ONLY_THIS);
        UpdateSections();
      }
      break;
    }

    default:
      CLog::Log(LOGWARNING, "CGUIWindowHome::OnPopupMenu can't handle choice %d", choice);
      return false;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::CheckTimer(const CStdString& strExisting, const CStdString& strNew, int title, int line1, int line2)
{
  bool bReturn;
  if (g_alarmClock.HasAlarm(strExisting) && strExisting != strNew)
  {
    if (CGUIDialogYesNo::ShowAndGetInput(title, line1, line2, 0, bReturn) == false)
    {
      return false;
    }
    else
    {
      g_alarmClock.Stop(strExisting, false);  
      return true;
    }
  }
  else
    return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::OnMessage(CGUIMessage& message)
{
  if (message.GetMessage() ==  GUI_MSG_WINDOW_DEINIT)
  {
    m_lastSelectedItem = GetCurrentItemName();
    HideAllLists();
    return true;
  }

  bool ret = CGUIWindow::OnMessage(message);

  switch (message.GetMessage())
  {
    case GUI_MSG_WINDOW_INIT:
    {
      UpdateSections();

      RestoreSection();

      if (m_lastSelectedItem == "Search")
        RefreshSection("global://art/", CPlexSectionFanout::SECTION_TYPE_GLOBAL_FANART);
      
      RefreshAllSections(false);
      g_plexApplication.themeMusicPlayer->playForItem(CFileItem());
      
      break;
    }

    case GUI_MSG_PLEX_BEST_SERVER_UPDATED:
    {
      RefreshAllSections(true);
      return true;
    }

    case GUI_MSG_PLEX_PLAYQUEUE_UPDATED:
    {
      // Add eventually the PQ Menu if we dont have it yet
      UpdateSections();

      RefreshSection("plexserver://playqueue/", CPlexSectionFanout::SECTION_TYPE_PLAYQUEUE);
      return true;
    }
      
    case GUI_MSG_WINDOW_RESET:
    case GUI_MSG_PLEX_SERVER_DATA_LOADED:
    case GUI_MSG_PLEX_SERVER_DATA_UNLOADED:
    case GUI_MSG_UPDATE:
    {
      UpdateSections();
      
      if (message.GetMessage() == GUI_MSG_WINDOW_RESET || message.GetMessage() == GUI_MSG_UPDATE)
        RefreshAllSections(false);
      else if (message.GetMessage() == GUI_MSG_PLEX_SERVER_DATA_LOADED)
        RefreshSectionsForServer(message.GetStringParam());

      RefreshSection("plexserver://playqueue/", CPlexSectionFanout::SECTION_TYPE_PLAYQUEUE);

      break;
    }

    case GUI_MSG_PLEX_SECTION_LOADED:
      OnSectionLoaded(message);
      return true;

    case GUI_MSG_CLICKED:
      if (message.GetParam1() == ACTION_SELECT_ITEM || message.GetParam1() == ACTION_PLAYER_PLAY)
        return OnClick(message);
      break;

    case GUI_MSG_PLEX_MULTIIMAGE_ROLLOVER:
      if (m_sections.find(m_currentFanArt) != m_sections.end())
      {
        CPlexSectionFanout *fan = m_sections[m_currentFanArt];
        fan->LoadArts();
      }

      break;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::OnSectionLoaded(const CGUIMessage& message)
{
  int type = message.GetParam1();
  CStdString url = message.GetStringParam();
  CFileItem* currentFileItem = GetCurrentFileItem();

  CStdString sectionToLoad;
  if (currentFileItem && currentFileItem->HasProperty("sectionPath"))
    sectionToLoad = currentFileItem->GetProperty("sectionPath").asString();
  if (m_lastSelectedItem != sectionToLoad)
    sectionToLoad = m_lastSelectedItem;

  if (type == CONTENT_LIST_FANART)
  {
    if ((url == sectionToLoad) || (url == "global://art/"))
    {
      CFileItemList list;
      if (GetContentListFromSection(url, CONTENT_LIST_FANART, list))
      {
        CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), SLIDESHOW_MULTIIMAGE, 0, 0, &list);
        OnMessage(msg);
        m_currentFanArt = url;
      }
      else
        CLog::Log(LOGDEBUG,
                  "CGUIWindowHome::OnMessage GetContentListFromSection returned empty list");
    }
  }
  else
  {
    if (url == sectionToLoad)
    {
      HideAllLists();

      std::vector<int> types;
      if (GetContentTypesFromSection(url, types))
      {
        BOOST_FOREACH(int p, types)
        {
          CFileItemList list;
          GetContentListFromSection(url, p, list);
          if(list.Size() > 0)
          {
            int selectedItem = 0;
            if (!m_lastSelectedSubItem.empty())
            {
              for (int i = 0; i < list.Size(); i ++)
              {
                if (list.Get(i)->GetPath() == m_lastSelectedSubItem)
                  selectedItem = i;
              }
            }

            CLog::Log(LOGDEBUG, "CGUIWindowHome::OnSectionLoaded showing %d with %d items", p, list.Size());
            CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), p, selectedItem, 0, &list);
            OnMessage(msg);
            SET_CONTROL_VISIBLE(p);

            // restore last focused controls
            if (p == m_focusSaver.getLastFocusedControlID())
             m_focusSaver.RestoreFocus(true);
          }
          else
            SET_CONTROL_HIDDEN(p);
        }
      }
    }
  }

}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::OnClick(const CGUIMessage& message)
{
  m_lastSelectedItem = GetCurrentItemName();

  int iAction = message.GetParam1();
  int currentContainer = GetFocusedControlID();

  CFileItemPtr fileItem = GetCurrentFanoutItem();

  if (fileItem)
  {
    if (fileItem->HasMusicInfoTag() &&
        (currentContainer == CONTENT_LIST_PLAYQUEUE_MUSIC ||
         currentContainer == CONTENT_LIST_PLAYQUEUE_VIDEO))
    {
      g_plexApplication.playQueueManager->playCurrentId(fileItem->GetMusicInfoTag()->GetDatabaseId());
    }
    else
    {
      if (iAction == ACTION_SELECT_ITEM && PlexUtils::CurrentSkinHasPreplay() &&
          fileItem->GetPlexDirectoryType() != PLEX_DIR_TYPE_PHOTO)
      {
        OpenItem(fileItem);
      }
      else if (fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_PHOTO ||
               fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_PHOTOALBUM)
      {
        if (fileItem->HasProperty("parentKey"))
          CApplicationMessenger::Get().PictureSlideShow(fileItem->GetProperty("parentKey").asString(),
                                                        false,
                                                        fileItem->GetPath());
        else
          CApplicationMessenger::Get().PictureShow(fileItem->GetPath());
      }
      else
      {
        g_plexApplication.playQueueManager->create(*fileItem);
      }
    }
  }
  else
  {
    if (iAction == ACTION_PLAYER_PLAY && GetCurrentItemName() == "plexserver://playqueue/")
    {
      g_plexApplication.playQueueManager->playCurrentId(-1);
      return true;
    }
    OpenItem(GetCurrentListItem());
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::OpenItem(CFileItemPtr item)
{
  // save current focused controls
  m_focusSaver.SaveFocus(this);

  CStdString url = m_navHelper.navigateToItem(item, CURL(), GetID());
  if (!url.empty())
    CLog::Log(LOGDEBUG, "CGUIWindowHome::OpenItem got %s back from navigateToItem, not sure what to do with it?", url.c_str());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  CPlexDirectoryFetchJob *fjob = static_cast<CPlexDirectoryFetchJob*>(job);
  m_cacheLoadFail = !success;

  if (success && fjob)
    g_directoryCache.SetDirectory(fjob->m_url.Get(), fjob->m_items, DIR_CACHE_ALWAYS);

  m_loadNavigationEvent.Set();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CGUIStaticItemPtr CGUIWindowHome::ItemToSection(CFileItemPtr item)
{
  CGUIStaticItemPtr newItem = CGUIStaticItemPtr(new CGUIStaticItem);
  newItem->SetLabel(item->GetLabel());
  newItem->SetLabel2(item->GetProperty("serverName").asString());
  newItem->SetProperty("sectionNameCollision", item->GetProperty("sectionNameCollision"));
  newItem->SetProperty("plex", true);
  newItem->SetProperty("sectionPath", item->GetPath());
  newItem->SetPlexDirectoryType(item->GetPlexDirectoryType());
  newItem->m_bIsFolder = true;

  bool useGlobalSlideshow = item->HasProperty("pref_includeInGlobal") ? !item->GetProperty("pref_includeInGlobal").asBoolean() : false;

  AddSection(item->GetPath(),
             CPlexSectionFanout::GetSectionTypeFromDirectoryType(item->GetPlexDirectoryType()), useGlobalSlideshow);

  return newItem;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static bool _sortLabels(const CGUIListItemPtr& item1, const CGUIListItemPtr& item2)
{
  if (item1->GetLabel() == item2->GetLabel())
    return (item1->GetLabel2() < item2->GetLabel2());
  
  if (item1->GetLabel2() == "myPlex")
    return false;
  if (item2->GetLabel2() == "myPlex")
    return true;

  return (item1->GetLabel() < item2->GetLabel());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
int CGUIWindowHome::GetPlayQueueType()
{
  return g_plexApplication.playQueueManager->getCurrentPlayQueuePlaylist();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::UpdateSections()
{
  CLog::Log(LOGDEBUG, "CGUIWindowHome::UpdateSections");

  CGUIBaseContainer* control = (CGUIBaseContainer*)GetControl(MAIN_MENU);
  if (!control)
  {
    CLog::Log(LOGWARNING, "CGUIWindowHome::UpdateSections can't find MAIN_MENU control");
    return;
  }

  vector<CGUIListItemPtr>& oldList = control->GetStaticItems();

  CFileItemListPtr sections = g_plexApplication.dataLoader->GetAllSections();
  vector<CGUIListItemPtr> newList;
  vector<CGUIListItemPtr> newSections;

  bool listUpdated = false;
  bool haveShared = false;
  bool haveChannels = false;
  bool haveUpdate = false;
  bool havePlayQueue = false;

  for (int i = 0; i < oldList.size(); i ++)
  {
    CGUIListItemPtr item = oldList[i];
    if (!item->HasProperty("plex"))
    {
      if (item->HasProperty("plexshared"))
      {
        haveShared = true;
        if (g_plexApplication.dataLoader->HasSharedSections())
          newList.push_back(item);
        else
          listUpdated = true;
      }
      else if (item->HasProperty("plexchannels"))
      {
        haveChannels = true;
        if (g_plexApplication.dataLoader->HasChannels())
          newList.push_back(item);
        else
          listUpdated = true;
      }
      else if (item->HasProperty("playqueue"))
      {
        havePlayQueue = true;
        if (GetPlayQueueType() != PLAYLIST_NONE)
          newList.push_back(item);
        else
          listUpdated = true;
      }
      else if (item->HasProperty("plexupdate"))
      {
        haveUpdate = true;
        newList.push_back(item);
      }
      else
        newList.push_back(item);
    }
    else
    {
      CFileItemPtr foundItem;
      for (int y = 0; y < sections->Size(); y++)
      {
        CFileItemPtr sectionItem = sections->Get(y);
        if(sectionItem->GetPath() == item->GetProperty("sectionPath").asString())
          foundItem = sectionItem;
      }

      if (foundItem)
      {
        /* If label or label2 has changed we need to update it */
        if (item->GetLabel() != foundItem->GetLabel())
        {
          listUpdated = true;
          item->SetLabel(foundItem->GetLabel());
        }
        item->SetProperty("sectionNameCollision", foundItem->GetProperty("sectionNameCollision"));

        newSections.push_back(item);
      }
      else
        /* this means that a server has been removed */
        listUpdated = true;
    }
  }

  for (int i = 0; i < sections->Size(); i++)
  {
    CFileItemPtr sectionItem = sections->Get(i);
    bool found = false;

    for(int y = 0; y < newSections.size(); y++)
    {
      CGUIListItemPtr item = newSections[y];

      if (item->GetProperty("sectionPath").asString() == sectionItem->GetPath())
      {
        found = true;
      }
    }

    if (!found)
    {
      newSections.push_back(ItemToSection(sectionItem));
      listUpdated = true;
    }
  }

  std::sort(newSections.begin(), newSections.end(), _sortLabels);
  for(int i = 0; i < newSections.size(); i ++)
  {
    CGUIListItemPtr item = newSections[i];
    newList.push_back(item);
  }

  if (g_plexApplication.dataLoader->HasChannels() && !haveChannels)
  {
    /* We need the channel button as well */
    CGUIStaticItemPtr item = CGUIStaticItemPtr(new CGUIStaticItem);
    item->SetLabel(g_localizeStrings.Get(52102));
    item->SetProperty("plexchannels", true);
    item->SetProperty("sectionPath", "plexserver://channels/");
    item->SetProperty("navigateDirectly", true);

    item->SetPath("XBMC.ActivateWindow(MyChannels,plexserver://channels,return)");
    item->SetClickActions(CGUIAction("", item->GetPath()));
    newList.push_back(item);
    listUpdated = true;

    AddSection("plexserver://channels/", CPlexSectionFanout::SECTION_TYPE_CHANNELS, false);
  }


  if (g_plexApplication.dataLoader->HasSharedSections() && !haveShared)
  {
    CGUIStaticItemPtr item = CGUIStaticItemPtr(new CGUIStaticItem);
    item->SetLabel(g_localizeStrings.Get(44020));
    item->SetProperty("plexshared", true);
    item->SetProperty("sectionPath", "plexserver://shared");
    item->SetPath("XBMC.ActivateWindow(MySharedContent,plexserver://shared,return)");
    item->SetClickActions(CGUIAction("", item->GetPath()));
    item->SetProperty("navigateDirectly", true);
    newList.push_back(item);
    listUpdated = true;
  }

  if (!havePlayQueue)
    AddPlayQueue(newList, listUpdated);

  if (listUpdated)
  {
    control->SetStaticContent(newList);
    RestoreSection();
  }

}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::AddPlayQueue(std::vector<CGUIListItemPtr>& list, bool& updated)
{
  int type = g_plexApplication.playQueueManager->getCurrentPlayQueuePlaylist();
  if (type == PLAYLIST_NONE)
    return;

  CFileItemList pq;
  if (!g_plexApplication.playQueueManager->getCurrentPlayQueue(pq))
    return;

  if (pq.Size() < 1)
    return;

  updated = true;

  CGUIStaticItemPtr item = CGUIStaticItemPtr(new CGUIStaticItem);
  CStdString path("plexserver://playqueue/");

  item->SetLabel("Play Queue");
  item->SetProperty("playqueue", true);
  item->SetPath("XBMC.ActivateWindow(PlexPlayQueue," + path + ",return)");
  item->SetClickActions(CGUIAction("", item->GetPath()));
  item->SetProperty("sectionPath", path);
  item->SetProperty("navigateDirectly", true);

  AddSection(path, CPlexSectionFanout::SECTION_TYPE_PLAYQUEUE, true);

  list.push_back(item);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::HideAllLists()
{
  // Hide lists.
  short lists[] = {CONTENT_LIST_ON_DECK,
                   CONTENT_LIST_RECENTLY_ACCESSED,
                   CONTENT_LIST_RECENTLY_ADDED,
                   CONTENT_LIST_QUEUE,
                   CONTENT_LIST_RECOMMENDATIONS,
                   CONTENT_LIST_PLAYQUEUE_MUSIC,
                   CONTENT_LIST_PLAYQUEUE_PHOTO,
                   CONTENT_LIST_PLAYQUEUE_VIDEO};

  BOOST_FOREACH(int id, lists)
  {
    SET_CONTROL_HIDDEN(id);
    SET_CONTROL_HIDDEN(id-1000);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::AddSection(const CStdString &url, CPlexSectionFanout::SectionTypes type,
                                bool useGlobalSlideshow)
{
  if (m_sections.find(url) == m_sections.end())
  {
    CLog::Log(LOG_LEVEL_DEBUG, "CGUIWindowHome::AddSection Adding section %s", url.c_str());
    CPlexSectionFanout* fan = new CPlexSectionFanout(url, type, useGlobalSlideshow);
    m_sections[url] = fan;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::RemoveSection(const CStdString &url)
{
  if (m_sections.find(url) != m_sections.end())
  {
    CPlexSectionFanout* fan = m_sections[url];
    m_sections.erase(url);
    delete fan;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::GetContentTypesFromSection(const CStdString &url, std::vector<int> &list)
{
  if (m_sections.find(url) != m_sections.end())
  {
    CPlexSectionFanout* section = m_sections[url];
    section->GetContentTypes(list);
    return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::GetContentListFromSection(const CStdString &url, int contentType, CFileItemList &l)
{
  if (m_sections.find(url) != m_sections.end())
  {
    CPlexSectionFanout* section = m_sections[url];
    section->GetContentList(contentType, l);
    if (l.Size() > 0 || contentType != CONTENT_LIST_FANART)
      return true;
  }

  if (contentType == CONTENT_LIST_FANART &&
      !g_guiSettings.GetBool("lookandfeel.enableglobalslideshow"))
  {
    /* Special case */
    CGUIBaseContainer *container = (CGUIBaseContainer*)GetControl(MAIN_MENU);
    CFileItemPtr defaultItem(new CFileItem(CPlexSectionFanout::GetBestServerUrl(":/resources/show-fanart.jpg"), false));

    if (!container)
      l.Add(defaultItem);

    BOOST_FOREACH(CGUIListItemPtr fileItem, container->GetItems())
    {
      if (fileItem->HasProperty("sectionPath") &&
          fileItem->GetProperty("sectionPath").asString() == url)
      {
        if (fileItem->HasArt(PLEX_ART_FANART))
          l.Add(CFileItemPtr(new CFileItem(fileItem->GetArt(PLEX_ART_FANART), false)));
        else
          l.Add(defaultItem);
        break;
      }
    }

    if (l.Size() == 0)
      l.Add(defaultItem);

    return true;
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::SectionNeedsRefresh(const CStdString &url)
{
  if (m_sections.find(url) != m_sections.end())
  {
    CPlexSectionFanout* section = m_sections[url];
    section->m_needsRefresh = true;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::OnTimeout()
{
  if (GetCurrentItemName() == m_lastSelectedItem)
  {
    CFileItemPtr pItem = GetCurrentListItem();
    if (!ShowSection(pItem->GetProperty("sectionPath").asString()) && !m_globalArt)
    {
      HideAllLists();
      ShowSection("global://art/");
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::RefreshSection(const CStdString &url, CPlexSectionFanout::SectionTypes type)
{
  if (m_sections.find(url) != m_sections.end())
  {
    CPlexSectionFanout* section = m_sections[url];
    return section->Refresh();
  }
  else
    AddSection(url, type, false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::RefreshAllSections(bool force)
{
  BOOST_FOREACH(nameSectionPair p, m_sections)
  {
    if (force || p.second->NeedsRefresh())
      p.second->Refresh();
  }

}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::RefreshSectionsForServer(const CStdString &uuid)
{
  BOOST_FOREACH(nameSectionPair p, m_sections)
  {
    CURL sectionUrl(p.first);
    if (sectionUrl.GetHostName() == uuid)
    {
      CLog::Log(LOGDEBUG, "CGUIWindowHome::RefreshSectionsForServer refreshing section %s because it belongs to server %s", p.first.c_str(), uuid.c_str());
      p.second->m_needsRefresh = true;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::ShowSection(const CStdString &url)
{
  if (m_sections.find(url) != m_sections.end())
  {
    CPlexSectionFanout* section = m_sections[url];
    section->Show();
    
    if (url == "global://art/")
      m_globalArt = true;
    else
      m_globalArt = false;
    
    return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowHome::ShowCurrentSection()
{
  if (GetCurrentItemName(true))
    return ShowSection(GetCurrentItemName(true));
  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CStdString CGUIWindowHome::GetCurrentItemName(bool onlySections)
{
  CStdString name;

  CFileItemPtr item = GetCurrentListItem();
  if (!item)
    return name;

  if (item->HasProperty("sectionPath"))
    name = item->GetProperty("sectionPath").asString();
  else if (!onlySections)
    name = item->GetLabel();

  return name;
}

///////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowHome::RestoreSection()
{
  if (m_lastSelectedItem == GetCurrentItemName())
  {
    ShowSection(m_lastSelectedItem);
    return;
  }

  HideAllLists();

  CGUIBaseContainer *pControl = (CGUIBaseContainer*)GetControl(MAIN_MENU);
  if (pControl && !m_lastSelectedItem.empty())
  {
    int idx = 0;
    BOOST_FOREACH(CGUIListItemPtr i, pControl->GetItems())
    {
      if (i->IsFileItem())
      {
        CFileItem* fItem = (CFileItem*)i.get();
        if (m_lastSelectedItem == fItem->GetProperty("sectionPath").asString() ||
            m_lastSelectedItem == fItem->GetLabel())
        {
          CGUIMessage msg(GUI_MSG_SETFOCUS, GetID(), pControl->GetID(), idx+1, 0);
          g_windowManager.SendThreadMessage(msg);

          if (fItem->HasProperty("sectionPath"))
            ShowSection(fItem->GetProperty("sectionPath").asString());
        }
      }
      idx++;
    }
  }
}

