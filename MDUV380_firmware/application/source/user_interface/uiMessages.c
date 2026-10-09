/*
 * Received text messages (Motorola TMS): the data service's inbox, newest first, with who sent them. Reading one here
 * doesn't take it from the USB host (dmrDataServiceInboxPop), nor the other way round
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "functions/dmrDataService.h"
#include "functions/voicePrompts.h"

#define MARGIN_X				4
#define SCROLL_MARK_WIDTH		6							// FONT_SIZE_1 is 6x8
#define NAME_COLUMNS			((DISPLAY_SIZE_X - (2 * MARGIN_X)) / 8)	// FONT_SIZE_2 is 8x8
#define TEXT_COLUMNS			((DISPLAY_SIZE_X - (2 * MARGIN_X) - SCROLL_MARK_WIDTH) / 8)// clear of the scroll marks
#define TEXT_LINE_HEIGHT		10
#define TEXT_Y					38
#define TEXT_LINES_VISIBLE		((DISPLAY_SIZE_Y - TEXT_Y) / TEXT_LINE_HEIGHT)
#define TEXT_LINES_MAX			24
#define AGE_REFRESH_MS			10000
#define NOTIFICATION_MS			5000
#define LINE_BUFFER_SIZE		(DISPLAY_SIZE_X / 6 + 1)	// FONT_SIZE_1 is 6x8

static int shownIndex = 0;
static int scrollLine = 0;
static uint32_t nextAgeRefresh;

// Start and length of each wrapped line of the shown message
static uint8_t lineStart[TEXT_LINES_MAX];
static uint8_t lineLength[TEXT_LINES_MAX];
static int lineCount;

static void updateScreen(bool playVP);
static void handleEvent(uiEvent_t *ev);

static void senderName(uint32_t src, char *buffer, size_t size)
{
	char name[MAX_DMR_ID_CONTACT_TEXT_LENGTH];

	if (!contactIDLookup(src, CONTACT_CALLTYPE_PC, name))
	{
		dmrIdDataStruct_t record;

		dmrIDLookup(src, &record);// "ID:n" when it isn't in the database
		snprintf(name, sizeof(name), "%s", record.text);
	}
	snprintf(buffer, size, "%s", name);
}

// Word wrap to TEXT_COLUMNS, breaking long words
static void wrapText(const char *text)
{
	int length = strlen(text);
	int start = 0;

	lineCount = 0;
	while ((start < length) && (lineCount < TEXT_LINES_MAX))
	{
		int end = start + TEXT_COLUMNS;

		if (end >= length)
		{
			end = length;
		}
		else
		{
			int space = end;

			while ((space > start) && (text[space] != ' '))
			{
				space--;
			}
			if (space > start)
			{
				end = space;
			}
		}

		lineStart[lineCount] = start;
		lineLength[lineCount] = end - start;
		lineCount++;

		start = end;
		while ((start < length) && (text[start] == ' '))
		{
			start++;
		}
	}
}

void dmrDataServiceMessageReceived(const dmrDataMessage_t *message)
{
	int current = menuSystemGetCurrentMenuNumber();

	displayLightTrigger(true);

	if (current == MENU_MESSAGES)
	{
		shownIndex = 0;
		scrollLine = 0;
		updateScreen(true);
	}
	else if (((current == UI_CHANNEL_MODE) || (current == UI_VFO_MODE)) && !trxTransmissionEnabled)
	{
		menuSystemPushNewMenu(MENU_MESSAGES);
	}
	else
	{
		char name[MAX_DMR_ID_CONTACT_TEXT_LENGTH];
		char text[NOTIFICATION_MESSAGE_LEN_MAX];

		senderName(message->src, name, sizeof(name));
		snprintf(text, sizeof(text), "Message from\n%s", name);
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, NOTIFICATION_MS, text, true);
	}
}

int uiMessagesUnreadCount(void)
{
	int unread = 0;

	for (int i = 0; i < dmrDataServiceMessageCount(); i++)
	{
		if (dmrDataServiceMessage(i)->unread)
		{
			unread++;
		}
	}

	return unread;
}

// The main menu entry (no translations: see MENU_STRING_MESSAGES)
const char *uiMessagesMenuLabel(void)
{
	static char label[SCREEN_LINE_BUFFER_SIZE];
	int unread = uiMessagesUnreadCount();

	if (unread > 0)
	{
		snprintf(label, sizeof(label), "Messages (%d)", unread);
	}
	else
	{
		snprintf(label, sizeof(label), "Messages");
	}

	return label;
}

menuStatus_t uiMessages(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		shownIndex = 0;// newest first
		scrollLine = 0;
		updateScreen(true);
	}
	else
	{
		if (ev->time > nextAgeRefresh)
		{
			updateScreen(false);
		}

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
	}

	return MENU_STATUS_SUCCESS;
}

static void updateScreen(bool playVP)
{
	char buffer[LINE_BUFFER_SIZE];
	int messageCount = dmrDataServiceMessageCount();

	nextAgeRefresh = ticksGetMillis() + AGE_REFRESH_MS;

	displayClearBuf();

	if (messageCount == 0)
	{
		menuDisplayTitle("Messages");
		displayPrintCentered((DISPLAY_SIZE_Y / 2) - 4, "No messages", FONT_SIZE_2);
		displayRender();

		if (playVP)
		{
			voicePromptsInit();
			voicePromptsAppendString("No messages");
			promptsPlayNotAfterTx();
		}
		return;
	}

	dmrDataMessage_t *message = dmrDataServiceMessage(shownIndex);
	uint32_t minutes = (ticksGetMillis() - message->receivedAt) / 60000U;
	char name[MAX_DMR_ID_CONTACT_TEXT_LENGTH];

	message->unread = false;

	snprintf(buffer, sizeof(buffer), "Message %d/%d", (shownIndex + 1), messageCount);
	menuDisplayTitle(buffer);

	senderName(message->src, name, sizeof(name));
	name[NAME_COLUMNS] = 0;
	displayThemeApply(THEME_ITEM_FG_MENU_NAME, THEME_ITEM_BG);
	displayPrintAt(MARGIN_X, 17, name, FONT_SIZE_2);

	if (minutes == 0)
	{
		snprintf(buffer, sizeof(buffer), "ID %u, now", (unsigned int)message->src);
	}
	else if (minutes < 60)
	{
		snprintf(buffer, sizeof(buffer), "ID %u, %um ago", (unsigned int)message->src, (unsigned int)minutes);
	}
	else
	{
		snprintf(buffer, sizeof(buffer), "ID %u, %uh ago", (unsigned int)message->src, (unsigned int)(minutes / 60));
	}
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintAt(MARGIN_X, 27, buffer, FONT_SIZE_1);

	wrapText(message->text);
	if (scrollLine > (lineCount - 1))
	{
		scrollLine = 0;
	}

	for (int i = 0; (i < TEXT_LINES_VISIBLE) && ((scrollLine + i) < lineCount); i++)
	{
		int line = scrollLine + i;

		memcpy(buffer, &message->text[lineStart[line]], lineLength[line]);
		buffer[lineLength[line]] = 0;
		displayPrintAt(MARGIN_X, TEXT_Y + (i * TEXT_LINE_HEIGHT), buffer, FONT_SIZE_2);
	}

	// More text above / below
	if (scrollLine > 0)
	{
		displayPrintAt(DISPLAY_SIZE_X - MARGIN_X - SCROLL_MARK_WIDTH, TEXT_Y, "^", FONT_SIZE_1);
	}
	if ((scrollLine + TEXT_LINES_VISIBLE) < lineCount)
	{
		displayPrintAt(DISPLAY_SIZE_X - MARGIN_X - SCROLL_MARK_WIDTH, TEXT_Y + ((TEXT_LINES_VISIBLE - 1) * TEXT_LINE_HEIGHT), "v", FONT_SIZE_1);
	}

	displayRender();

	if (playVP)
	{
		voicePromptsInit();
		voicePromptsAppendString(name);
		promptsPlayNotAfterTx();
	}
}

// Up / down scroll through the text, then on to the next message (newer above, older below)
static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(false);
		return;
	}

	if (EVENTCHECK_SHORTUP(ev->keys))
	{
		switch (ev->keys.key)
		{
			case KEY_RED:
			case KEY_GREEN:
				menuSystemPopPreviousMenu();
				return;

			case KEY_UP:
				if (scrollLine > 0)
				{
					scrollLine--;
					updateScreen(false);
				}
				else if (shownIndex > 0)
				{
					shownIndex--;
					updateScreen(true);
				}
				break;

			case KEY_DOWN:
				if ((scrollLine + TEXT_LINES_VISIBLE) < lineCount)
				{
					scrollLine++;
					updateScreen(false);
				}
				else if (shownIndex < (dmrDataServiceMessageCount() - 1))
				{
					shownIndex++;
					scrollLine = 0;
					updateScreen(true);
				}
				break;
		}
	}
}
