#ifndef FE_STOCK_APPS_H
#define FE_STOCK_APPS_H
/*
 * fe_stock_apps.h — which of the 7 built-in (stock) launcher apps the Spare
 * launcher shows. Clean-room, Spare original. Config only: no code reads it
 * yet; the front-end owner wires it into the grid seeding (see
 * docs/frontend-v1.md "隐藏的原厂 App").
 *
 * The phone app (Spare io) serves todo, prompter and aiSubtitle; focusmode is
 * standalone. recorder, conversationAssist and ai_memory have no phone-side
 * service yet — opening them would spin forever waiting for the vendor phone
 * app — so they are hidden for now.
 *
 * Ids are the stock app package names; FE_STOCK_IDX_* is the stock launcher
 * index the port's stock_label/stock_icon_l8/stock_launch take.
 */

#define FE_STOCK_IDX_RECORDER            0
#define FE_STOCK_IDX_AI_SUBTITLE         1
#define FE_STOCK_IDX_CONVERSATION_ASSIST 2
#define FE_STOCK_IDX_TODO                3
#define FE_STOCK_IDX_PROMPTER            4
#define FE_STOCK_IDX_AI_MEMORY           5
#define FE_STOCK_IDX_FOCUSMODE           6

/* Visible stock apps, in launcher order. */
#define FE_STOCK_VISIBLE_IDS \
  { "com.rayneo.liteos.aiSubtitle", "com.rayneo.liteos.todo", \
    "com.rayneo.liteos.prompter", "com.rayneo.liteos.focusmode" }

#define FE_STOCK_VISIBLE_IDX \
  { FE_STOCK_IDX_AI_SUBTITLE, FE_STOCK_IDX_TODO, FE_STOCK_IDX_PROMPTER, FE_STOCK_IDX_FOCUSMODE }

#define FE_STOCK_VISIBLE_COUNT 4

/* Bitmask over stock indices (bit i set = visible): 1,3,4,6. */
#define FE_STOCK_VISIBLE_MASK \
  ((1u << FE_STOCK_IDX_AI_SUBTITLE) | (1u << FE_STOCK_IDX_TODO) | \
   (1u << FE_STOCK_IDX_PROMPTER) | (1u << FE_STOCK_IDX_FOCUSMODE))

#endif /* FE_STOCK_APPS_H */
