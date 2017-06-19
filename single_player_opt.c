#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>

typedef enum {RED, YELLOW, GREEN, BLUE} QColor;

typedef enum
{
  PENALTY,           // 0

  WHITE_AS_RED,      // 1
  WHITE_AS_YELLOW,
  WHITE_AS_GREEN,
  WHITE_AS_BLUE,

  LOW_RED_ONLY,                    // 5
  WHITE_AS_RED_THEN_LOW_RED,
  WHITE_AS_YELLOW_THEN_LOW_RED,
  WHITE_AS_GREEN_THEN_LOW_RED,
  WHITE_AS_BLUE_THEN_LOW_RED,

  LOW_YELLOW_ONLY,                 // 10
  WHITE_AS_RED_THEN_LOW_YELLOW,
  WHITE_AS_YELLOW_THEN_LOW_YELLOW,
  WHITE_AS_GREEN_THEN_LOW_YELLOW,
  WHITE_AS_BLUE_THEN_LOW_YELLOW,

  HI_GREEN_ONLY,                   // 15
  WHITE_AS_RED_THEN_HI_GREEN,
  WHITE_AS_YELLOW_THEN_HI_GREEN,
  WHITE_AS_GREEN_THEN_HI_GREEN,
  WHITE_AS_BLUE_THEN_HI_GREEN,

  HI_BLUE_ONLY,                    // 20
  WHITE_AS_RED_THEN_HI_BLUE,
  WHITE_AS_YELLOW_THEN_HI_BLUE,
  WHITE_AS_GREEN_THEN_HI_BLUE,
  WHITE_AS_BLUE_THEN_HI_BLUE,

  // These options are only of interest when potentially locking a color
  HI_RED_ONLY,                     // 25
  WHITE_AS_RED_THEN_HI_RED,
  WHITE_AS_YELLOW_THEN_HI_RED,
  WHITE_AS_GREEN_THEN_HI_RED,
  WHITE_AS_BLUE_THEN_HI_RED,

  HI_YELLOW_ONLY,                  // 30
  WHITE_AS_RED_THEN_HI_YELLOW,
  WHITE_AS_YELLOW_THEN_HI_YELLOW,
  WHITE_AS_GREEN_THEN_HI_YELLOW,
  WHITE_AS_BLUE_THEN_HI_YELLOW,

  LOW_GREEN_ONLY,                  // 35
  WHITE_AS_RED_THEN_LOW_GREEN,
  WHITE_AS_YELLOW_THEN_LOW_GREEN,
  WHITE_AS_GREEN_THEN_LOW_GREEN,
  WHITE_AS_BLUE_THEN_LOW_GREEN,

  LOW_BLUE_ONLY,                   // 40
  WHITE_AS_RED_THEN_LOW_BLUE,
  WHITE_AS_YELLOW_THEN_LOW_BLUE,
  WHITE_AS_GREEN_THEN_LOW_BLUE,
  WHITE_AS_BLUE_THEN_LOW_BLUE,

  NUM_ACTIONS

} QAction;

#define NUM_COLORS               4
#define NUM_SINGLE_COLOR_STATES 57
#define NUM_DUAL_COLOR_STATES   (NUM_SINGLE_COLOR_STATES*(NUM_SINGLE_COLOR_STATES+1)/2) //    1653
#define NUM_FOUR_COLOR_STATES   (NUM_DUAL_COLOR_STATES  *(NUM_DUAL_COLOR_STATES  +1)/2) // 1367031
#define NUM_TOTAL_STATES        (NUM_FOUR_COLOR_STATES  * 4 + 1)                        // 5468125

typedef struct

{
    QColor color;
    int    numMarks;
    int    rightMark; // 2-12
} QColorState;

typedef struct
{
    QColorState color[NUM_COLORS];
    int numPenalties;
} QwixxState;

static float *Wvec = NULL;
static int c1c2ToCombined[NUM_SINGLE_COLOR_STATES][NUM_SINGLE_COLOR_STATES];
static int c12c24ToCombined[NUM_DUAL_COLOR_STATES][NUM_DUAL_COLOR_STATES];
static int quadToC12[NUM_FOUR_COLOR_STATES];
static int quadToC34[NUM_FOUR_COLOR_STATES];
static int dualToC1 [NUM_DUAL_COLOR_STATES];
static int dualToC2 [NUM_DUAL_COLOR_STATES];

static inline float getWforState(QwixxState *state);
static inline int colorStateTo62State(QColorState *color);

inline int canTakeMark(QwixxState *state, int color, int diceVal, int numPenalties, int colorStates[], 
                int *newStateIx, int newColorStates[], float *newStateW)
{
    int retVal = 0;

    if (color == RED || color == YELLOW)
    {
        retVal = diceVal >       state->color[color].rightMark && 
                (diceVal < 12 || state->color[color].numMarks >= 5);
    }
    else
    {
        retVal = (diceVal <       state->color[color].rightMark || state->color[color].numMarks == 0) && 
                 (diceVal >  2 || state->color[color].numMarks >= 5);
    }
    if (retVal && newStateIx)
    {
        QwixxState newQstate = *state;
        newQstate.color[color].rightMark = diceVal;
        newQstate.color[color].numMarks++;
        //printf("Calling getWState for color %d diceVal %d\n", color, diceVal);
        *newStateW = getWforState(&newQstate);

        // FIXME - maybe not needed anymore??
        int newColorState = colorStateTo62State(&newQstate.color[color]);
        newColorStates[0] = colorStates[0];
        newColorStates[1] = colorStates[1];
        newColorStates[2] = colorStates[2];
        newColorStates[3] = colorStates[3];
        newColorStates[color] = newColorState;
        *newStateIx = numPenalties           * 62*62*62*62 +
                      newColorStates[RED   ] * 62*62*62    +
                      newColorStates[YELLOW] * 62*62       +
                      newColorStates[GREEN ] * 62          +
                      newColorStates[BLUE  ];          
    }

    return retVal;
}

void colorIx2State(int ix, QColorState *color)
{
  if (ix == 0)
  {
    color->rightMark = 0;
    color->numMarks  = 0;
  }
  else if (ix <= 1)
  {
    color->rightMark = 2;
    color->numMarks  = ix - 0;
  }
  else if (ix <= 3)
  {
    color->rightMark = 3;
    color->numMarks  = ix - 1;
  }
  else if (ix <= 6)
  {
    color->rightMark = 4;
    color->numMarks  = ix - 3;
  }
  else if (ix <= 10)
  {
    color->rightMark = 5;
    color->numMarks  = ix - 6;
  }
  else if (ix <= 15)
  {
    color->rightMark = 6;
    color->numMarks  = ix - 10;
  }
  else if (ix <= 21)
  {
    color->rightMark = 7;
    color->numMarks  = ix - 15;
  }
  else if (ix <= 28)
  {
    color->rightMark = 8;
    color->numMarks  = ix - 21;
  }
  else if (ix <= 36)
  {
    color->rightMark = 9;
    color->numMarks  = ix - 28;
  }
  else if (ix <= 45)
  {
    color->rightMark = 10;
    color->numMarks  = ix - 36;
  }
  else if (ix <= 55)
  {
    color->rightMark = 11;
    color->numMarks  = ix - 45;
  }
  else if (ix <= 61)
  {
    color->rightMark = 12;       // locked
    color->numMarks  = ix - 55 + 5;
  }
  if (color->color == GREEN || color->color == BLUE)
  {
    if (color->rightMark > 0)
    {
      color->rightMark = 14 - color->rightMark;
    }
  }
}

void constructStateFromIx(int ix, QwixxState *state)
{
  state->numPenalties = ix / (62*62*62*62);
  int allColorIx      = ix % (62*62*62*62);
  int blueIx   =  allColorIx               % 62;
  int greenIx  = (allColorIx /  62       ) % 62;
  int yellowIx = (allColorIx / (62*62   )) % 62;
  int redIx    = (allColorIx / (62*62*62)) % 62;

  colorIx2State(redIx   , &state->color[RED   ]);
  colorIx2State(yellowIx, &state->color[YELLOW]);
  colorIx2State(greenIx , &state->color[GREEN ]);
  colorIx2State(blueIx  , &state->color[BLUE  ]);
}

static inline int colorStateTo62State(QColorState *color)
{
  static const int lookupTbl[13][12] =
    {
                                   /* Num Marks */
                       /*   0   1   2   3   4   5   6   7   8   9  10  11  */
      /* lastMark =  0 */ { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  1 */ { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  2 */ { 0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  3 */ { 0,  2,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  4 */ { 0,  4,  5,  6,  0,  0,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  5 */ { 0,  7,  8,  9, 10,  0,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  6 */ { 0, 11, 12, 13, 14, 15,  0,  0,  0,  0,  0,  0 },
      /* lastMark =  7 */ { 0, 16, 17, 18, 19, 20, 21,  0,  0,  0,  0,  0 },
      /* lastMark =  8 */ { 0, 22, 23, 24, 25, 26, 27, 28,  0,  0,  0,  0 },
      /* lastMark =  9 */ { 0, 29, 30, 31, 32, 33, 34, 35, 36,  0,  0,  0 },
      /* lastMark = 10 */ { 0, 37, 38, 39, 40, 41, 42, 43, 44, 45,  0,  0 },
      /* lastMark = 11 */ { 0, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,  0 },
      /* lastMark = 12 */ { 0,  0,  0,  0,  0,  0, 56, 57, 58, 59, 60, 61 } };

  int rightMark = color->rightMark;
  if (color->color == GREEN || color->color == BLUE)
  {
    if (rightMark > 0)
    {
      rightMark = 14 - rightMark;
    }
  }

  return lookupTbl[rightMark][color->numMarks];
}

static void initLookupTables()
{
  // Form c1c2ToCombined
  int c1, c2, c12, c34, c1234;

  memset(c1c2ToCombined, 0, sizeof(c1c2ToCombined));
  memset(dualToC1,       0, sizeof(dualToC1));
  memset(dualToC2,       0, sizeof(dualToC2));
  c12 = 0;
  for (c1 = 0; c1 < NUM_SINGLE_COLOR_STATES; c1++)
  {
    for (c2 = c1; c2 < NUM_SINGLE_COLOR_STATES; c2++)
    {
      c1c2ToCombined[c1][c2] = c12;
      dualToC1[c12]          = c1;
      dualToC2[c12]          = c2;
      c12++;
    }
  }

  memset(c12c24ToCombined, 0, sizeof(c12c24ToCombined));
  memset(quadToC12,        0, sizeof(quadToC12));
  memset(quadToC34,        0, sizeof(quadToC34));
  c1234 = 0;
  for (c12 = 0; c12 < NUM_DUAL_COLOR_STATES; c12++)
  {
    for (c34 = c12; c34 < NUM_DUAL_COLOR_STATES; c34++)
    {
      c12c24ToCombined[c12][c34] = c1234;
      quadToC12[c1234]           = c12;
      quadToC34[c1234]           = c34;
      c1234++;
    }
  }
}

static inline int convertClipped5tupleToIx(int redIx, int yellowIx, int greenIx, int blueIx, int numPenalties)
{
  int retVal = 0;

  int ry, gb, rygb, tmp;

  if (numPenalties >= 4)
  {
    retVal = NUM_TOTAL_STATES - 1;
  }
  else
  {
    // Force redIx <= yellowIx
    if (redIx > yellowIx)
    {
      tmp      = redIx;
      redIx    = yellowIx;
      yellowIx = tmp;
    }

    // Force greenIx <= blueIx
    if (greenIx > blueIx)
    {
      tmp     = greenIx;
      greenIx = blueIx;
      blueIx  = tmp;
    }

    ry = c1c2ToCombined[  redIx][yellowIx];
    gb = c1c2ToCombined[greenIx][  blueIx];

    // Force ry <= gb
    if (ry > gb)
    {
      tmp = gb;
      gb  = ry;
      ry  = tmp;
    }

    rygb = c12c24ToCombined[ry][gb];

    //printf("convert5tupleToIx(%d,%d,%d,%d,%d) --> ry %d, gb %d, rygb %d\n",
    //   redIx, yellowIx, greenIx, blueIx, numPenalties, ry, gb, rygb);

    retVal = numPenalties * NUM_FOUR_COLOR_STATES + rygb;
  }

  return retVal;
}

static inline int convert5tupleToIx(int redIx, int yellowIx, int greenIx, int blueIx, int numPenalties)
{
  int retVal = 0;

  //Clip to be <= 56
  int new_redIx    = (redIx   <=56) ? redIx    : 56;
  int new_yellowIx = (yellowIx<=56) ? yellowIx : 56;
  int new_greenIx  = (greenIx <=56) ? greenIx  : 56;
  int new_blueIx   = (blueIx  <=56) ? blueIx   : 56;

  retVal = convertClipped5tupleToIx(new_redIx, new_yellowIx, new_greenIx, new_blueIx, numPenalties);

  return retVal;
}

static inline int convertRYGBPToIx(int RYGB[], int numPenalties)
{
  return convert5tupleToIx(RYGB[0], RYGB[1], RYGB[2], RYGB[3], numPenalties);
}

static inline int convertStateToIx(QwixxState *state)
{
  int retVal = 0;
  int redIx    = colorStateTo62State(&state->color[RED   ]);
  int yellowIx = colorStateTo62State(&state->color[YELLOW]);
  int greenIx  = colorStateTo62State(&state->color[GREEN ]);
  int blueIx   = colorStateTo62State(&state->color[BLUE  ]);

  retVal = convert5tupleToIx(redIx, yellowIx, greenIx, blueIx, state->numPenalties);

  return retVal;
}

int isGameOver(QwixxState *state)
{
  int retVal = 0;
  if (state->numPenalties >= 4)
  {
    retVal = 1;
  }
  else
  {
    int numLocked = 0;
    numLocked += (state->color[RED   ].rightMark == 12) ? 1 : 0;
    numLocked += (state->color[YELLOW].rightMark == 12) ? 1 : 0;
    numLocked += (state->color[GREEN ].rightMark ==  2) ? 1 : 0;
    numLocked += (state->color[BLUE  ].rightMark ==  2) ? 1 : 0;
    if (numLocked >= 2)
    {
      retVal = 1;
    }
  }

  return retVal;
}

int getColorScore(QColorState *color)
{
  int scores[] = {0,1,3,6,10,15,21,28,36,45,55,66,78};
  int isLocked = 0;
  if (color->color == RED || color->color == YELLOW)
  {
    isLocked = (color->rightMark == 12) ? 1 : 0;
  }
  else
  {
    isLocked = (color->rightMark ==  2) ? 1 : 0;
  }

  return scores[isLocked + color->numMarks];
}

int getScore(QwixxState *state)
{
  int score = 0;

  score -= 5 * state->numPenalties;
  score += getColorScore(&state->color[RED   ]);
  score += getColorScore(&state->color[YELLOW]);
  score += getColorScore(&state->color[GREEN ]);
  score += getColorScore(&state->color[BLUE  ]);

  return score;
}

static float getWforState(QwixxState *state)
{
  float retVal = 0.0;
  int ix = 0;

  int redIx    = 0;
  int yellowIx = 0;
  int greenIx  = 0;
  int blueIx   = 0;
  int rClipped = 0;
  int yClipped = 0;
  int gClipped = 0;
  int bClipped = 0;
  int warning  = 0;
  int valIsClipped = 0;

  if (isGameOver(state))
  {
    retVal = (float) getScore(state);
  }
  else
  {
    redIx    = colorStateTo62State(&state->color[RED   ]);
    yellowIx = colorStateTo62State(&state->color[YELLOW]);
    greenIx  = colorStateTo62State(&state->color[GREEN ]);
    blueIx   = colorStateTo62State(&state->color[BLUE  ]);

    if (redIx > 56)
    {
      rClipped     = 56;
      valIsClipped = 1;
    }
    else
    {
      rClipped = redIx;
    }
    if (yellowIx > 56)
    {
      yClipped     = 56;
      valIsClipped = 1;
    }
    else
    {
      yClipped = yellowIx;
    }
    if (greenIx > 56)
    {
      gClipped     = 56;
      valIsClipped = 1;
    }
    else
    {
      gClipped = greenIx;
    }
    if (blueIx > 56)
    {
      bClipped     = 56;
      valIsClipped = 1;
    }
    else
    {
      bClipped = blueIx;
    }

    // 62stateIx =                  57,   58,   59,   60,   61
    static const float offset[] = {8.0, 17.0, 27.0, 38.0, 50.0};

    ix = convertClipped5tupleToIx(rClipped, yClipped, gClipped, bClipped, state->numPenalties);

    retVal = Wvec[ix];
    if (retVal == -1e15f)
    {
      printf("WARNING!\n");
      warning = 1;
    }

    // Account for the fact that 56 undershoots some states
    if (valIsClipped)
    {
      retVal += 
         ((redIx    > rClipped ) ? offset[redIx    - rClipped - 1] : 0.0f) +
         ((yellowIx > yClipped ) ? offset[yellowIx - yClipped - 1] : 0.0f) +
         ((greenIx  > gClipped ) ? offset[greenIx  - gClipped - 1] : 0.0f) +
         ((blueIx   > bClipped ) ? offset[blueIx   - bClipped - 1] : 0.0f);
    }
  }

  if (warning)
  {
    printf("State %d [%d R:%d/%d(%d) Y:%d/%d(%d) G:%d/%d(%d) B:%d/%d(%d)], Wvec %.1f\n",
           ix, state->numPenalties,
           state->color[0].numMarks, state->color[0].rightMark, redIx,
           state->color[1].numMarks, state->color[1].rightMark, yellowIx,
           state->color[2].numMarks, state->color[2].rightMark, greenIx,
           state->color[3].numMarks, state->color[3].rightMark, blueIx,
           retVal);
  }

  return retVal;
}

inline int pickBestAction(double rewards[], int iStart, int iStop, int iBestBeforeStart)
{
  int bestIdx = iBestBeforeStart;
  double bestReward = rewards[iBestBeforeStart];
  int loopIdx;

  for (loopIdx = iStart; loopIdx <= iStop; loopIdx++)
  {
    if (rewards[loopIdx] > bestReward)
    {
      bestReward = rewards[loopIdx];
      bestIdx = loopIdx;
    }
  }

  return bestIdx;
}

static void *calculateWvec(void *parms)
{
  int p = 0;
  int s = 0;
  int RYGB[4] = {0};
  const double inv6_to6 = 1.0 / (6*6*6*6*6*6);

  QwixxState state;
  memset(&state, 0, sizeof(state));
  state.color[RED   ].color = RED;
  state.color[YELLOW].color = YELLOW;
  state.color[GREEN ].color = GREEN;
  state.color[BLUE  ].color = BLUE;

  for (s = NUM_TOTAL_STATES - 1; s >= 0; s--)
  {
    int numPenalties = s / NUM_FOUR_COLOR_STATES; // integer division
    int rygbState    = s % NUM_FOUR_COLOR_STATES;
    if (numPenalties >= 4)
    {
      Wvec[s] = -1e100;
    }
    else
    {
      int ry = quadToC12[rygbState];
      int gb = quadToC34[rygbState];

      RYGB[0] = dualToC1[ry];
      RYGB[1] = dualToC2[ry];
      RYGB[2] = dualToC1[gb];
      RYGB[3] = dualToC2[gb];

      state.numPenalties = numPenalties;
      colorIx2State(RYGB[0], &state.color[RED   ]);
      colorIx2State(RYGB[1], &state.color[YELLOW]);
      colorIx2State(RYGB[2], &state.color[GREEN ]);
      colorIx2State(RYGB[3], &state.color[BLUE  ]);

      // Shortcut for number of penalties
      p = numPenalties;

      if (isGameOver(&state))
      {
        Wvec[s] = -1e100;
      }
      else
      {
        // Roll the dice
        int w1, w2, r, y, g, b;
        double   actionReward[NUM_ACTIONS];
        int    newColorStates[NUM_COLORS];
        double theWnext = 0.0;

        // "Was" means "White as"
        QwixxState newTmpStateTookWasRED;
        QwixxState newTmpStateTookWasYELLOW;
        QwixxState newTmpStateTookWasGREEN;
        QwixxState newTmpStateTookWasBLUE;
        int        newTmpColStateTookWasRED   [NUM_COLORS];
        int        newTmpColStateTookWasYELLOW[NUM_COLORS];
        int        newTmpColStateTookWasGREEN [NUM_COLORS];
        int        newTmpColStateTookWasBLUE  [NUM_COLORS];
        int        canTakeWasRED    = 0;
        int        canTakeWasYELLOW = 0;
        int        canTakeWasGREEN  = 0;
        int        canTakeWasBLUE   = 0;

        {
          QwixxState stateWithAddtlPenalty = state;
          stateWithAddtlPenalty.numPenalties++;
          actionReward[PENALTY] = getWforState(&stateWithAddtlPenalty);
        }

        for (w1 = 1; w1 <= 6; w1++)
        {
          // w2 is always >= w1
          for (w2 = w1; w2 <= 6; w2++)
          {
            int w = w1 + w2;
            // count some cases twice due to w2 looping limits optimizations
            double pScale = (w1 == w2) ? inv6_to6 : (2*inv6_to6);
            int newStateIx = 0;
            float newStateW = 0.0f;

#define CHECK_W_AS_COLOR(COLORUPPER)                                                                                            \
            actionReward[WHITE_AS_##COLORUPPER] = -1e9;                                                                         \
            canTakeWas##COLORUPPER = 0;                                                                                         \
            if (canTakeMark(&state, COLORUPPER, w, p, RYGB, &newStateIx, newTmpColStateTookWas##COLORUPPER, &newStateW))        \
            {                                                                                                                   \
              actionReward[WHITE_AS_##COLORUPPER] = newStateW;                                                                  \
              newTmpStateTookWas##COLORUPPER = state;                                                                           \
              colorIx2State(newTmpColStateTookWas##COLORUPPER[COLORUPPER], &newTmpStateTookWas##COLORUPPER.color[COLORUPPER]);  \
              canTakeWas##COLORUPPER = ! isGameOver(&newTmpStateTookWas##COLORUPPER);                                           \
            }

            CHECK_W_AS_COLOR(RED)
            CHECK_W_AS_COLOR(YELLOW)
            CHECK_W_AS_COLOR(GREEN)
            CHECK_W_AS_COLOR(BLUE)

            int bestThruWhiteOnly = pickBestAction(actionReward, WHITE_AS_RED, WHITE_AS_BLUE, PENALTY);

            for (r = 1; r <= 6; r++)
            {
#define CHECK_LOW_C1_ONLY(COLOR1UPPER, colorDiceVal)                                                             \
              actionReward[LOW_##COLOR1UPPER##_ONLY] = -1e9;                                                     \
              if (canTakeMark(&state, COLOR1UPPER, w1+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))       \
              {                                                                                                  \
                actionReward[LOW_##COLOR1UPPER##_ONLY] = newStateW;                                              \
              }                                                                                                  \
              else if (canTakeMark(&state, COLOR1UPPER, w2+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))  \
              {                                                                                                  \
                actionReward[LOW_##COLOR1UPPER##_ONLY] = newStateW;                                              \
              }

#define CHECK_HI_C1_ONLY(COLOR1UPPER, colorDiceVal)                                                              \
              actionReward[HI_##COLOR1UPPER##_ONLY] = -1e9;                                                      \
              if (canTakeMark(&state, COLOR1UPPER, w2+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))       \
              {                                                                                                  \
                actionReward[HI_##COLOR1UPPER##_ONLY] = newStateW;                                               \
              }                                                                                                  \
              else if (canTakeMark(&state, COLOR1UPPER, w1+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))  \
              {                                                                                                  \
                actionReward[HI_##COLOR1UPPER##_ONLY] = newStateW;                                               \
              }

#define CHECK_W_AS_C1_THEN_C2_LOW(COLOR1UPPER, COLOR2UPPER, colorDiceVal)                               \
              actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = -1e9;                     \
              if (canTakeWas##COLOR1UPPER)                                                              \
              {                                                                                         \
                if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w1+colorDiceVal, p,      \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))       \
                {                                                                                       \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = newStateW;            \
                }                                                                                       \
                else if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w2+colorDiceVal, p, \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))       \
                {                                                                                       \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = newStateW;            \
                }                                                                                       \
              }

#define CHECK_W_AS_C1_THEN_C2_HI(COLOR1UPPER, COLOR2UPPER, colorDiceVal)                                \
              actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = -1e9;                      \
              if (canTakeWas##COLOR1UPPER)                                                              \
              {                                                                                         \
                if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w2+colorDiceVal, p,      \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))       \
                {                                                                                       \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = newStateW;             \
                }                                                                                       \
                else if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w1+colorDiceVal, p, \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))       \
                {                                                                                       \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = newStateW;             \
                }                                                                                       \
              }

              CHECK_LOW_C1_ONLY(RED, r)
              CHECK_W_AS_C1_THEN_C2_LOW(RED   , RED, r)
              CHECK_W_AS_C1_THEN_C2_LOW(YELLOW, RED, r)
              CHECK_W_AS_C1_THEN_C2_LOW(GREEN , RED, r)
              CHECK_W_AS_C1_THEN_C2_LOW(BLUE  , RED, r)

              int bestThruRed = pickBestAction(actionReward, 
                          LOW_RED_ONLY, WHITE_AS_BLUE_THEN_LOW_RED, bestThruWhiteOnly);

              // If it is possible to lock red, then check the high options, too
              if (r == 6 && w2 == 6)
              {
                CHECK_HI_C1_ONLY(RED, r);
                CHECK_W_AS_C1_THEN_C2_HI(RED   , RED, r)
                CHECK_W_AS_C1_THEN_C2_HI(YELLOW, RED, r)
                CHECK_W_AS_C1_THEN_C2_HI(GREEN , RED, r)
                CHECK_W_AS_C1_THEN_C2_HI(BLUE  , RED, r)
                bestThruRed = pickBestAction(actionReward, 
                          HI_RED_ONLY, WHITE_AS_BLUE_THEN_HI_RED, bestThruRed);
              }

              for (y = 1; y <= 6; y++)
              {
                CHECK_LOW_C1_ONLY(YELLOW, y)
                CHECK_W_AS_C1_THEN_C2_LOW(RED   , YELLOW, y)
                CHECK_W_AS_C1_THEN_C2_LOW(YELLOW, YELLOW, y)
                CHECK_W_AS_C1_THEN_C2_LOW(GREEN , YELLOW, y)
                CHECK_W_AS_C1_THEN_C2_LOW(BLUE  , YELLOW, y)

                int bestThruYellow = pickBestAction(actionReward, 
                            LOW_YELLOW_ONLY, WHITE_AS_BLUE_THEN_LOW_YELLOW, bestThruRed);

                // If it is possible to lock yellow, then check the high options, too
                if (y == 6 && w2 == 6)
                {
                  CHECK_HI_C1_ONLY(YELLOW, y);
                  CHECK_W_AS_C1_THEN_C2_HI(RED   , YELLOW, y)
                  CHECK_W_AS_C1_THEN_C2_HI(YELLOW, YELLOW, y)
                  CHECK_W_AS_C1_THEN_C2_HI(GREEN , YELLOW, y)
                  CHECK_W_AS_C1_THEN_C2_HI(BLUE  , YELLOW, y)
                  bestThruYellow = pickBestAction(actionReward, 
                            HI_YELLOW_ONLY, WHITE_AS_BLUE_THEN_HI_YELLOW, bestThruYellow);
                }

                for (g = 1; g <= 6; g++)
                {
                  CHECK_HI_C1_ONLY(GREEN, g)
                  CHECK_W_AS_C1_THEN_C2_HI(RED   , GREEN, g)
                  CHECK_W_AS_C1_THEN_C2_HI(YELLOW, GREEN, g)
                  CHECK_W_AS_C1_THEN_C2_HI(GREEN , GREEN, g)
                  CHECK_W_AS_C1_THEN_C2_HI(BLUE  , GREEN, g)

                  int bestThruGreen = pickBestAction(actionReward, 
                              HI_GREEN_ONLY, WHITE_AS_BLUE_THEN_HI_GREEN, bestThruYellow);

                  // If it is possible to lock green, then check the low green, too
                  if (g == 1 && w1 == 1)
                  {
                    CHECK_LOW_C1_ONLY(GREEN, g);
                    CHECK_W_AS_C1_THEN_C2_LOW(RED   , GREEN, g)
                    CHECK_W_AS_C1_THEN_C2_LOW(YELLOW, GREEN, g)
                    CHECK_W_AS_C1_THEN_C2_LOW(GREEN , GREEN, g)
                    CHECK_W_AS_C1_THEN_C2_LOW(BLUE  , GREEN, g)
                    bestThruGreen = pickBestAction(actionReward, 
                              LOW_GREEN_ONLY, WHITE_AS_BLUE_THEN_LOW_GREEN, bestThruGreen);
                  }

                  for (b = 1; b <= 6; b++)
                  {
                    CHECK_HI_C1_ONLY(BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(RED   , BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(YELLOW, BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(GREEN , BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(BLUE  , BLUE, b)

                    int bestThruBlue = pickBestAction(actionReward, 
                                HI_BLUE_ONLY, WHITE_AS_BLUE_THEN_HI_BLUE, bestThruGreen);

                    // If it is possible to lock blue, then check the low blue, too
                    if (b == 1 && w1 == 1)
                    {
                      CHECK_LOW_C1_ONLY(BLUE, b);
                      CHECK_W_AS_C1_THEN_C2_LOW(RED   , BLUE, b)
                      CHECK_W_AS_C1_THEN_C2_LOW(YELLOW, BLUE, b)
                      CHECK_W_AS_C1_THEN_C2_LOW(GREEN , BLUE, b)
                      CHECK_W_AS_C1_THEN_C2_LOW(BLUE  , BLUE, b)
                      bestThruBlue = pickBestAction(actionReward, 
                                LOW_BLUE_ONLY, WHITE_AS_BLUE_THEN_LOW_BLUE, bestThruBlue);
                    }

                    theWnext += actionReward[bestThruBlue] * pScale;

                    if (0)//actionReward[bestThruBlue] > 0.0 && bestThruBlue != PENALTY)
                    {
                      printf("State %d [%d R:%d/%d Y:%d/%d G:%d/%d B:%d/%d], "
                             "Dice [W:%d %d R:%d Y:%d G:%d B:%d] Action %d Reward %.1f "
                             "Rewards: %.1f %.1f %.1f %.1f %.1f\n",
                             s, p, 
                             state.color[0].numMarks, state.color[0].rightMark,
                             state.color[1].numMarks, state.color[1].rightMark,
                             state.color[2].numMarks, state.color[2].rightMark,
                             state.color[3].numMarks, state.color[3].rightMark,
                             w1, w2, r, y, g, b, bestThruBlue, actionReward[bestThruBlue],
                             actionReward[0],
                             actionReward[1],
                             actionReward[2],
                             actionReward[3],
                             actionReward[4]);
                    }
                  } // b
                } //g
              } // y
            } // r
          } // w2
        } // w1
        Wvec[s] = theWnext;
      } // end else game not over
    } // end else numPenalties < 4
    if ((NUM_TOTAL_STATES - s) % 1000 == 0)
    {
      printf("% 8d / % 8d states complete\n", NUM_TOTAL_STATES - s, NUM_TOTAL_STATES);
    }
  } // end s loop
} // end calculateWvec()

int main(int argc, char *argv[])
{
    FILE *fp = NULL;
    char filenameBuf[128];

    initLookupTables();

    Wvec = malloc(sizeof(float)*NUM_TOTAL_STATES);
    int i;
    for (i = 0; i < NUM_TOTAL_STATES; i++)
    {
      Wvec[i] = -1e15f;
    }

    calculateWvec(NULL);

    snprintf(filenameBuf, sizeof(filenameBuf), "qwixx.bin");
    printf("Saving results to %s ...\n", filenameBuf);
    fp = fopen(filenameBuf,"wb");
    if (fp)
    {
      fwrite(Wvec, sizeof(float)*NUM_TOTAL_STATES, 1, fp);
      fclose(fp);
    }

    return 0;
}
