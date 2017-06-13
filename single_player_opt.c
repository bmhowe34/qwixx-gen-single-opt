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

#define NUM_COLORS 4
#define NUM_TOTAL_STATES (5*62*62*62*62) // 73881680

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

inline int canTakeMark(QwixxState *state, int color, int diceVal, int numPenalties, int colorStates[], 
                int *newStateIx, int newColorStates[])
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
        QColorState newState = state->color[color];
        int newColorState;
        newState.rightMark = diceVal;
        newState.numMarks++;
        newColorState = colorStateToIx(&newState);
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

inline int colorStateToIx(QColorState *color)
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

int convertStateToIx(QwixxState *state)
{
  int retVal = 0;
  int redIx    = colorStateToIx(&state->color[RED   ]);
  int yellowIx = colorStateToIx(&state->color[YELLOW]);
  int greenIx  = colorStateToIx(&state->color[GREEN ]);
  int blueIx   = colorStateToIx(&state->color[BLUE  ]);

  retVal += blueIx;
  retVal += greenIx             * 62;
  retVal += yellowIx            * 62 * 62;
  retVal += redIx               * 62 * 62 * 62;
  retVal += state->numPenalties * 62 * 62 * 62 * 62;

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

float *Wprev = NULL;
float *Wnext = NULL;

void calculateW0(float *W0)
{
  int p = 0;
  int s = 0;
  int RYGB[4] = {0};
  memset(&W0[0], 0, sizeof(float)*NUM_TOTAL_STATES);

  QwixxState state;
  memset(&state, 0, sizeof(state));
  state.color[RED   ].color = RED;
  state.color[YELLOW].color = YELLOW;
  state.color[GREEN ].color = GREEN;
  state.color[BLUE  ].color = BLUE;

  for (p = 0; p <= 4; p++)
  {
    state.numPenalties = p;
    for (RYGB[0] = 0; RYGB[0] < 62; RYGB[0]++)
    {
      colorIx2State(RYGB[0], &state.color[RED]);
      for (RYGB[1] = 0; RYGB[1] < 62; RYGB[1]++)
      {
        colorIx2State(RYGB[1], &state.color[YELLOW]);
        for (RYGB[2] = 0; RYGB[2] < 62; RYGB[2]++)
        {
          colorIx2State(RYGB[2], &state.color[GREEN]);
          for (RYGB[3] = 0; RYGB[3] < 62; RYGB[3]++)
          {
            colorIx2State(RYGB[3], &state.color[BLUE]);
            W0[s] = (float) getScore(&state);
            s++;
          }
        }
      }
    }
  }

  printf("Analyzed %d states for W0\n", s);
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

typedef struct
{
  int zeroBasedThreadID;
  int nThreads;
  int nStatesDone;
  pthread_t thread_id;
} CalcWNextParms;

static void *calculateWnext(void *parms)
{
  int p = 0;
  int s = 0;
  int RYGB[4] = {0};
  const double inv6_to6 = 1.0 / (6*6*6*6*6*6);
  CalcWNextParms *wnParms = (CalcWNextParms *) parms;

  int nStatesPerThread = (NUM_TOTAL_STATES - 62*62*62*62 + wnParms->nThreads - 1) / wnParms->nThreads;
  int myStartState = wnParms->zeroBasedThreadID * nStatesPerThread;
  int myStopState  = (wnParms->zeroBasedThreadID + 1) * nStatesPerThread;

  if (wnParms->zeroBasedThreadID == wnParms->nThreads - 1)
  {
    myStopState = NUM_TOTAL_STATES;
  }

  QwixxState state;
  memset(&state, 0, sizeof(state));
  state.color[RED   ].color = RED;
  state.color[YELLOW].color = YELLOW;
  state.color[GREEN ].color = GREEN;
  state.color[BLUE  ].color = BLUE;

  wnParms->nStatesDone = 0;

  if (myStopState > NUM_TOTAL_STATES)
  {
    myStopState = NUM_TOTAL_STATES;
  }

  for (p = 0; p <= 4; p++)
  {
    state.numPenalties = p;
    s = p * 62 * 62 * 62 * 62;
    for (RYGB[0] = 0; RYGB[0] < 62; RYGB[0]++)
    {
      colorIx2State(RYGB[0], &state.color[RED]);
      for (RYGB[1] = 0; RYGB[1] < 62; RYGB[1]++)
      {
        colorIx2State(RYGB[1], &state.color[YELLOW]);
        for (RYGB[2] = 0; RYGB[2] < 62; RYGB[2]++)
        {
          colorIx2State(RYGB[2], &state.color[GREEN]);
          for (RYGB[3] = 0; RYGB[3] < 62; RYGB[3]++)
          {
            colorIx2State(RYGB[3], &state.color[BLUE]);

            if (s < myStartState || s >= myStopState)
            {
              // Do nothing because it's not my work to do
            }
            else if (isGameOver(&state))
            {
              // Do nothing. Carry over previous results
              Wnext[s] = Wprev[s];
              wnParms->nStatesDone++;
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

              actionReward[PENALTY] = Wprev[s + 62*62*62*62];

              for (w1 = 1; w1 <= 6; w1++)
              {
                // w2 is always >= w1
                for (w2 = w1; w2 <= 6; w2++)
                {
                  int w = w1 + w2;
                  // count some cases twice due to w2 looping limits optimizations
                  double pScale = (w1 == w2) ? inv6_to6 : (2*inv6_to6);
                  int newStateIx = 0;

#define CHECK_W_AS_COLOR(COLORUPPER)                                                                                                  \
                  actionReward[WHITE_AS_##COLORUPPER] = -1e9;                                                                         \
                  canTakeWas##COLORUPPER = 0;                                                                                         \
                  if (canTakeMark(&state, COLORUPPER, w, p, RYGB, &newStateIx, newTmpColStateTookWas##COLORUPPER))                    \
                  {                                                                                                                   \
                    actionReward[WHITE_AS_##COLORUPPER] = Wprev[newStateIx];                                                          \
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
#define CHECK_LOW_C1_ONLY(COLOR1UPPER, colorDiceVal)                                                                   \
                    actionReward[LOW_##COLOR1UPPER##_ONLY] = -1e9;                                                     \
                    if (canTakeMark(&state, COLOR1UPPER, w1+colorDiceVal, p, RYGB, &newStateIx, newColorStates))       \
                    {                                                                                                  \
                      actionReward[LOW_##COLOR1UPPER##_ONLY] = Wprev[newStateIx];                                      \
                    }                                                                                                  \
                    else if (canTakeMark(&state, COLOR1UPPER, w2+colorDiceVal, p, RYGB, &newStateIx, newColorStates))  \
                    {                                                                                                  \
                      actionReward[LOW_##COLOR1UPPER##_ONLY] = Wprev[newStateIx];                                      \
                    }

#define CHECK_HI_C1_ONLY(COLOR1UPPER, colorDiceVal)                                                                    \
                    actionReward[HI_##COLOR1UPPER##_ONLY] = -1e9;                                                      \
                    if (canTakeMark(&state, COLOR1UPPER, w2+colorDiceVal, p, RYGB, &newStateIx, newColorStates))       \
                    {                                                                                                  \
                      actionReward[HI_##COLOR1UPPER##_ONLY] = Wprev[newStateIx];                                       \
                    }                                                                                                  \
                    else if (canTakeMark(&state, COLOR1UPPER, w1+colorDiceVal, p, RYGB, &newStateIx, newColorStates))  \
                    {                                                                                                  \
                      actionReward[HI_##COLOR1UPPER##_ONLY] = Wprev[newStateIx];                                       \
                    }

#define CHECK_W_AS_C1_THEN_C2_LOW(COLOR1UPPER, COLOR2UPPER, colorDiceVal)                                     \
                    actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = -1e9;                     \
                    if (canTakeWas##COLOR1UPPER)                                                              \
                    {                                                                                         \
                      if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w1+colorDiceVal, p,      \
                          newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates))                   \
                      {                                                                                       \
                        actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = Wprev[newStateIx];    \
                      }                                                                                       \
                      else if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w2+colorDiceVal, p, \
                          newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates))                   \
                      {                                                                                       \
                        actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = Wprev[newStateIx];    \
                      }                                                                                       \
                    }

#define CHECK_W_AS_C1_THEN_C2_HI(COLOR1UPPER, COLOR2UPPER, colorDiceVal)                                      \
                    actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = -1e9;                      \
                    if (canTakeWas##COLOR1UPPER)                                                              \
                    {                                                                                         \
                      if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w2+colorDiceVal, p,      \
                          newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates))                   \
                      {                                                                                       \
                        actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = Wprev[newStateIx];     \
                      }                                                                                       \
                      else if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w1+colorDiceVal, p, \
                          newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates))                   \
                      {                                                                                       \
                        actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = Wprev[newStateIx];     \
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
              wnParms->nStatesDone++;
              Wnext[s] = (float) theWnext;
            } // else game not over
            s++;
            //if (p==0 && s%1000==0){printf("s = %d\n", s);}
          }
        }
      }
    }
  } // end p loop
  printf("End thread ID %d\n", wnParms->zeroBasedThreadID);
}

#define MAX_THREADS 1024

void CalcWNextWithMultiThreads(int nThreads)
{
  CalcWNextParms parms[MAX_THREADS];
  int s[MAX_THREADS];
  void *res;
  int loop;
  int done = 0;

  for (loop = 0; loop < nThreads; loop++)
  {
    parms[loop].zeroBasedThreadID = loop;
    parms[loop].nThreads = nThreads;
    parms[loop].nStatesDone = 0;

    s[loop] = pthread_create(&parms[loop].thread_id, NULL,
                    &calculateWnext, &parms[loop]);
  }

  while (!done)
  {
    int nStatesDone = 0;
    for (loop = 0; loop < nThreads; loop++)
    {
      nStatesDone += parms[loop].nStatesDone;
    }
    if (nStatesDone >= NUM_TOTAL_STATES)
    {
      done = 1;
    }
    printf("nStatesDone = %d\n", nStatesDone);
    sleep(1);
  }

  for (loop = 0; loop < nThreads; loop++)
  {
    s[loop] = pthread_join(parms[loop].thread_id, &res);
  }
}

int main(int argc, char *argv[])
{
    FILE *fp = NULL;
    size_t bytes_read = 0;
    int nThreads = 0;
    int Wnum = 0;
    char filenameBuf[128];

    if (argc > 2)
    {
      Wnum = atoi(argv[1]);
      nThreads = atoi(argv[2]);
      if (nThreads > 0 && nThreads < MAX_THREADS)
      {
        printf("Program will use %d threads\n", nThreads);
      }
      else
      {
        printf("Command line argument said to use %d threads but that is out of range...will just use 1\n", nThreads);
        nThreads = 1;
      }
    }
    else if (argc > 1)
    {
      Wnum = atoi(argv[1]);
      nThreads = 1;
    }
    else
    {
      Wnum = 0;
      nThreads = 1;
    }

    printf("Calculating Wnum = %d\n", Wnum);

    Wprev = malloc(sizeof(float)*NUM_TOTAL_STATES);
    Wnext = malloc(sizeof(float)*NUM_TOTAL_STATES);

    if (Wnum == 0)
    {
      calculateW0(Wnext);
    }
    else if (Wnum > 0)
    {
      snprintf(filenameBuf, sizeof(filenameBuf), "qwixxN_W%02d.bin", Wnum-1);
      printf("Reading %s for Wprev...\n", filenameBuf);
      fp = fopen(filenameBuf,"rb");
      if (fp)
      {
        bytes_read = fread(Wprev, sizeof(float)*NUM_TOTAL_STATES, 1, fp);
        fclose(fp);
      }
      else
      {
        printf("File not found...exiting\n");
        return -1;
      }

      memset(Wnext,  0, sizeof(float)*NUM_TOTAL_STATES);

      // Calculate Wnext
      printf("Calculating Wnext ...\n");
      CalcWNextWithMultiThreads(nThreads);
      //calculateWnext();
    }

    snprintf(filenameBuf, sizeof(filenameBuf), "qwixxN_W%02d.bin", Wnum);
    printf("Saving results to %s ...\n", filenameBuf);
    fp = fopen(filenameBuf,"wb");
    if (fp)
    {
      fwrite(Wnext, sizeof(float)*NUM_TOTAL_STATES, 1, fp);
      fclose(fp);
    }

    return 0;
}
