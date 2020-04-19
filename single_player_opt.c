#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>

// The 4 Qwixx colors
typedef enum {RED, YELLOW, GREEN, BLUE} QColor;

#define NUM_COLORS 4

typedef enum
{
  PENALTY,                         // 0

  WHITE_AS_RED,                    // 1
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

  // These options are only of interest when potentially locking a color. For
  // example, when NOT locking a color, if given a choice between LOW_RED_ONLY
  // (the lower sum of w1+red) and HI_RED_ONLY (the higher sum of (w2+red), you
  // would normally never choose HI_RED_ONLY because that just *takes away*
  // from future possible points. However, when the higher sum (w2+red) would
  // lock the red row, that is worthy of being considered every time it is
  // possible, as that may be the best choice.
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

  NUM_ACTIONS                      // 45

} QAction;

// As shown in Table 1 of:
// https://drive.google.com/file/d/0B0E4VFlFjnCuME9sZGhrbGRIWXc/view
// There are 62 states for each color. Using 1-based numbers, they are described below.
// State   1    is  when the row is empty
// State   2    is  when the there is a single mark, and it is in box "2"
// States  3- 4 are when the last mark is in box "3", and there are 1-2 marks in the row, respectively
// States  5- 7 are when the last mark is in box "4", and there are 1-3 marks in the row, respectively
// States  8-11 are when the last mark is in box "5", and there are 1-4 marks in the row, respectively
// States 12-16 are when the last mark is in box "6", and there are 1-5 marks in the row, respectively
// ...
// States 47-56 are when the last mark is in box "11", and there are 1-10 marks in the row, respectively
// States 57-62 are when the last mark is in box "12", and there are 6-11 marks in the row, respectively
//    Note: you can't mark box "12" until you have already taken 5 marks.

// Given that there are 4 colors, and 5 possible penalty values (0, 1, 2, 3,
// and 4), there are technically 62*62*62*62*5 = 73881680 "game" states.
#define NUM_GAME_STATES (62*62*62*62*5)

// Courtesy u/chaotic_iak:
// While there are 62 states the row can be in, states 58-62 are all identical
// to state 57, except they are offset by a constant number of points, so we
// only need to actually track 57 states.
#define NUM_SINGLE_COLOR_STATES 57

// Courtesy u/chaotic_iak:
// From a Markov probability perspective, red and yellow are essentially
// equivalent.  That is - "3 red marks, last red 6; 4 yellow marks, last yellow
// 9" is equivalent to "4 red marks, last red 9; 3 yellow marks, last yellow
// 6". So treat them as the same Markov state. The same applies to green and
// blue, respectively. Note that as a result, we are only populating the upper
// triangular portion of c1c2ToCombined[][].
#define NUM_DUAL_COLOR_STATES   (NUM_SINGLE_COLOR_STATES*(NUM_SINGLE_COLOR_STATES+1)/2) //    1653

// Same logic as above, but it is for the *combination* of RY/GB.  We are only
// populating the upper triangular portion of c12c34ToCombined[][].
#define NUM_FOUR_COLOR_STATES   (NUM_DUAL_COLOR_STATES*(NUM_DUAL_COLOR_STATES+1)/2)     // 1367031

// 4 possible penalty values before game ends (0, 1, 2, or 3). Add one state
// for the end game state. This is the total number of states that we'll track
// in the Markov matrix. Note that this is only ~7.4% of the total number of
// "game" states.
#define NUM_MARKOV_STATES       (NUM_FOUR_COLOR_STATES * 4 + 1)                         // 5468125

// Set Wvec[] values for "end of game" states to invalid because you can't
// actually count a score for these states (due to the 62-->57 state reduction
// above).
#define WVEC_END_OF_GAME (-1e100)

// This is the state of a single color row on the Qwixx score sheet.
typedef struct

{
    QColor color;
    int    numMarks;
    int    rightMark; // 2-12
} QColorState;

// This is the state of the whole Qwixx score sheet
typedef struct
{
    QColorState color[NUM_COLORS];
    int numPenalties;
} QwixxState;

// This is the expected final score under optimal decisions for all possible states.
static float *Wvec = NULL;

// Combines 2 colors' [0-NUM_SINGLE_COLOR_STATES-1] states to the [0-NUM_DUAL_COLOR_STATES-1] range
static int c1c2ToCombined[NUM_SINGLE_COLOR_STATES][NUM_SINGLE_COLOR_STATES];

// Combines the red/yellow [0-NUM_DUAL_COLOR_STATES-1] with green/blue
// [0-NUM_DUAL_COLOR_STATES-1] states to [0-NUM_FOUR_COLOR_STATES-1] range
static int c12c34ToCombined[NUM_DUAL_COLOR_STATES][NUM_DUAL_COLOR_STATES];

// Reverse lookup of c12c34ToCombined[][]
// Maps [0-NUM_FOUR_COLOR_STATES-1] down to the [0-NUM_DUAL_COLOR_STATES-1] range; picked such that c12 <= c34
static int quadToC12[NUM_FOUR_COLOR_STATES];
static int quadToC34[NUM_FOUR_COLOR_STATES];

// Reverse lookup of c1c2ToCombined[][]
// Maps [0-NUM_DUAL_COLOR_STATES-1] down to the [0-NUM_SINGLE_COLOR_STATES-1] range; picked such that c1 <= c2
static int dualToC1 [NUM_DUAL_COLOR_STATES];
static int dualToC2 [NUM_DUAL_COLOR_STATES];

// Function prototypes
static inline float getWforState       (QwixxState  *state);
static inline float getWforStateOpt    (QwixxState  *state, int r62ix, int y62ix, int g62ix, int b62ix);
static inline int   colorStateTo62State(QColorState *color);

static inline int getScore(QwixxState *state);

// Returns 1 if it is legal to take a move; 0 otherwise Also returns the W
// value for that new state. Note that this function is called A LOT.
inline int canTakeMark(QwixxState *state, int color, int diceVal, int numPenalties,
                int colorStates[],       // the [0-61] color state for each of the 4 colors (INPUT)
                // Outputs
                int   *newStateIx,       // 0-NUM_GAME_STATES-1
                int    newColorStates[], // The state of each color, [0-61]
                float *newStateW)        // The expected score corresponding to *newStateIx
{
    int retVal = 0;

    if (color == RED || color == YELLOW)
    {
        retVal = diceVal >       state->color[color].rightMark &&
                (diceVal < 12 || state->color[color].numMarks >= 5);
    }
    else // green or blue
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

        // Calculate newColorStates[]
        newColorStates[0]     = colorStates[0];
        newColorStates[1]     = colorStates[1];
        newColorStates[2]     = colorStates[2];
        newColorStates[3]     = colorStates[3];
        newColorStates[color] = colorStateTo62State(&newQstate.color[color]);

        // Shortcut to see if the game is over by seeing if two or more colors
        // are locked. This has to be done before calling getWforStateOpt().
        if ( (int)(newColorStates[0] >= 56) +
             (int)(newColorStates[1] >= 56) +
             (int)(newColorStates[2] >= 56) +
             (int)(newColorStates[3] >= 56) >= 2)
        {
          *newStateW = getScore(&newQstate);
        }
        else
        {
          *newStateW = getWforStateOpt(&newQstate,
                                       newColorStates[0],
                                       newColorStates[1],
                                       newColorStates[2],
                                       newColorStates[3]);
        }

        *newStateIx = numPenalties           * 62*62*62*62 +
                      newColorStates[RED   ] * 62*62*62    +
                      newColorStates[YELLOW] * 62*62       +
                      newColorStates[GREEN ] * 62          +
                      newColorStates[BLUE  ];
    }

    return retVal;
}

// Update a QColorState's 'rightMark' and 'numMarks' corresponding to input ix (ix range: 0-61)
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

  // Red and yellow count up and are handled correctly above.  If this is green
  // or blue, then they count down. Adjust rightMark accordingly.
  if (color->color == GREEN || color->color == BLUE)
  {
    if (color->rightMark > 0)
    {
      color->rightMark = 14 - color->rightMark;
    }
  }
}

// Construct QwixxState from a state index (range 0-NUM_GAME_STATES-1
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

// Convert a QColorState to a state index (0-61)
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
  // Form c1c2ToCombined and c12c34ToCombined
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

  memset(c12c34ToCombined, 0, sizeof(c12c34ToCombined));
  memset(quadToC12,        0, sizeof(quadToC12));
  memset(quadToC34,        0, sizeof(quadToC34));
  c1234 = 0;
  for (c12 = 0; c12 < NUM_DUAL_COLOR_STATES; c12++)
  {
    for (c34 = c12; c34 < NUM_DUAL_COLOR_STATES; c34++)
    {
      c12c34ToCombined[c12][c34] = c1234;
      quadToC12[c1234]           = c12;
      quadToC34[c1234]           = c34;
      c1234++;
    }
  }
}

// Convert ALREADY CLIPPED values to a state index (output range 0..NUM_MARKOV_STATES-1)
static inline int convertClipped5tupleToIx(int redIx, int yellowIx, int greenIx, int blueIx, int numPenalties)
{
  int retVal = 0;

  int ry, gb, rygb, tmp;

  if (numPenalties >= 4)
  {
    retVal = NUM_MARKOV_STATES - 1;
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

    rygb = c12c34ToCombined[ry][gb];

    //printf("convert5tupleToIx(%d,%d,%d,%d,%d) --> ry %d, gb %d, rygb %d\n",
    //   redIx, yellowIx, greenIx, blueIx, numPenalties, ry, gb, rygb);

    retVal = numPenalties * NUM_FOUR_COLOR_STATES + rygb;
  }

  return retVal;
}

static inline int isGameOver(QwixxState *state)
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

static inline int getColorScore(QColorState *color)
{
  // Index by number of marks. Add 1 for "lock" bonus
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

static inline int getScore(QwixxState *state)
{
  int score = 0;

  // -5 points per penalty
  score -= 5 * state->numPenalties;
  score += getColorScore(&state->color[RED   ]);
  score += getColorScore(&state->color[YELLOW]);
  score += getColorScore(&state->color[GREEN ]);
  score += getColorScore(&state->color[BLUE  ]);

  return score;
}

// Optimized function for getWforState. There are 2 critical assumptions that
// must be met before using this optimized version of the function.
// 1. The "62 index" values must be consistent with the "state" parameter.
// 2. The state must not correspond to a "game over" state
static inline float getWforStateOpt(QwixxState *state, int r62ix, int y62ix, int g62ix, int b62ix)
{
  float retVal     = 0.0;
  int ix           = 0;

  int rClipped     = 0;
  int yClipped     = 0;
  int gClipped     = 0;
  int bClipped     = 0;
  int warning      = 0;
  int valIsClipped = 0;

#define CLIP_CHECK(colorIx, cClipped, valIsClipped)  \
  if (colorIx > 56)                                  \
  {                                                  \
    cClipped     = 56;                               \
    valIsClipped = 1;                                \
  }                                                  \
  else                                               \
  {                                                  \
    cClipped = colorIx;                              \
  }

  CLIP_CHECK(r62ix, rClipped, valIsClipped)
  CLIP_CHECK(y62ix, yClipped, valIsClipped)
  CLIP_CHECK(g62ix, gClipped, valIsClipped)
  CLIP_CHECK(b62ix, bClipped, valIsClipped)

  // The following array helps account that state 56 is equivalent to states
  // 57-61, except for the number of points that has been earned by the state.

  // 62stateIx =                  57,   58,   59,   60,   61
  static const float offset[] = {8.0, 17.0, 27.0, 38.0, 50.0};

  ix = convertClipped5tupleToIx(rClipped, yClipped, gClipped, bClipped, state->numPenalties);

  retVal = Wvec[ix];
  if (retVal == WVEC_END_OF_GAME)
  {
    printf("WARNING!\n");
    warning = 1;
  }

  // Account for the fact that 56 undershoots some states
  if (valIsClipped)
  {
    retVal +=
       ((r62ix > rClipped ) ? offset[r62ix - rClipped - 1] : 0.0f) +
       ((y62ix > yClipped ) ? offset[y62ix - yClipped - 1] : 0.0f) +
       ((g62ix > gClipped ) ? offset[g62ix - gClipped - 1] : 0.0f) +
       ((b62ix > bClipped ) ? offset[b62ix - bClipped - 1] : 0.0f);
  }

  if (warning)
  {
    printf("State %d [%d R:%d/%d(%d) Y:%d/%d(%d) G:%d/%d(%d) B:%d/%d(%d)], Wvec %.1f\n",
           ix, state->numPenalties,
           state->color[0].numMarks, state->color[0].rightMark, r62ix,
           state->color[1].numMarks, state->color[1].rightMark, y62ix,
           state->color[2].numMarks, state->color[2].rightMark, g62ix,
           state->color[3].numMarks, state->color[3].rightMark, b62ix,
           retVal);
    exit(-1);
  }

  return retVal;
}

// Prior to the state reduction optimization, this function was a trival
// conversion from QwixxState to an index, followed by a lookup into Wvec[]
// with the new index.  However, it is a bit more complicated now that we have
// reduced from NUM_GAME_STATES down to NUM_MARKOV_STATES states.
static float getWforState(QwixxState *state)
{
  float retVal     = 0.0;
  int   redIx      = 0;
  int   yellowIx   = 0;
  int   greenIx    = 0;
  int   blueIx     = 0;

  // Wvec[] is invalid for a "game over" state since a single "game over" state
  // can correspond to many actual scores.
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

    retVal   = getWforStateOpt(state, redIx, yellowIx, greenIx, blueIx);
  }

  return retVal;
}

// Pick the best action out of the iStart...iStop possibilities
inline int pickBestAction(double rewards[], int iStart, int iStop, int iBestBeforeStart)
{
  int    bestIdx    = iBestBeforeStart;
  double bestReward = rewards[iBestBeforeStart];
  int    loopIdx;

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

// This is the main function that calculates the W vector. It takes a very long time.
static void calculateWvec(int num_iterations)
{
  int          p                = 0;  // shortcut for number of penalties
  int          s                = 0;  // state loop index
  int          RYGB[NUM_COLORS] = {0};

  // Probability of each 6-dice throw (each die has 6 possible outcomes)
  const double inv6_to6 = 1.0 / (6*6*6*6*6*6);

  // Allow the main loop to be cut short for timing purposes
  int stopping_state_index = 0;
  if (num_iterations > 0)
  {
    stopping_state_index = NUM_MARKOV_STATES - num_iterations;
    if (stopping_state_index < 0)
    {
      stopping_state_index = 0;
    }
  }

  QwixxState state;
  memset(&state, 0, sizeof(state));
  state.color[RED   ].color = RED;
  state.color[YELLOW].color = YELLOW;
  state.color[GREEN ].color = GREEN;
  state.color[BLUE  ].color = BLUE;

  // Loop through the states backwards. Each loop only looks at state indices
  // >= this current s, so it only works when looping backward.
  for (s = NUM_MARKOV_STATES - 1; s >= stopping_state_index; s--)
  {
    int numPenalties = s / NUM_FOUR_COLOR_STATES; // integer division
    int rygbState    = s % NUM_FOUR_COLOR_STATES;
    if (numPenalties >= 4)
    {
      // Set Wvec[s] to invalid because you can't actually count a score for
      // "end of game" Wvec states.
      Wvec[s] = WVEC_END_OF_GAME;
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
        // Set Wvec[s] to invalid because you can't actually count a score for
        // "end of game" Wvec states.
        Wvec[s] = WVEC_END_OF_GAME;
      }
      else
      {
        // Roll the dice (two white dice, red, yellow, green, blue)
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

        QwixxState stateWithAddtlPenalty = state;
        stateWithAddtlPenalty.numPenalties++;
        actionReward[PENALTY] = getWforState(&stateWithAddtlPenalty);

        for (w1 = 1; w1 <= 6; w1++)
        {
          // Optimization: w2 is always >= w1
          for (w2 = w1; w2 <= 6; w2++)
          {
            int w = w1 + w2;
            // count some cases twice due to w2 looping limits optimizations
            double pScale = (w1 == w2) ? inv6_to6 : (2*inv6_to6);
            int newStateIx = 0;
            float newStateW = 0.0f;

// This macro checks to see if you can take the sum of the two white dice as
// one of the colors. This is the first choice a Qwixx player must evaluate.
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

            // Evaluate the best option that has been calculated so far
            int bestThruWhiteOnly = pickBestAction(actionReward, WHITE_AS_RED, WHITE_AS_BLUE, PENALTY);

            for (r = 1; r <= 6; r++)
            {
// CHECK_LOW_C1_ONLY:
// This macro checks to see if you can take the lower of (w1,w2) (which is
// always w1) plus a colored die as that color. This is denoted as "Choice 1"
// (C1). If w1+colorDiceVal is unable to be played, evaluate whether or not you
// can take w2+colorDiceVal. This populates the actionReward for:
// - LOW_RED_ONLY
// - LOW_YELLOW_ONLY
// - LOW_GREEN_ONLY
// - LOW_BLUE_ONLY
#define CHECK_LOW_C1_ONLY(COLOR1UPPER, colorDiceVal)                                                                         \
              actionReward[LOW_##COLOR1UPPER##_ONLY] = -1e9;                                                                 \
              if (canTakeMark(&state, COLOR1UPPER, w1+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))       \
              {                                                                                                              \
                actionReward[LOW_##COLOR1UPPER##_ONLY] = newStateW;                                                          \
              }                                                                                                              \
              else if (canTakeMark(&state, COLOR1UPPER, w2+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))  \
              {                                                                                                              \
                actionReward[LOW_##COLOR1UPPER##_ONLY] = newStateW;                                                          \
              }

// CHECK_HI_C1_ONLY:
// Same as CHECK_LOW_C1_ONLY, except it evaluates w2+colorDiceVal first and
// w1+colorDiceVal second. This populates the actionReward for:
// - HI_RED_ONLY
// - HI_YELLOW_ONLY
// - HI_GREEN_ONLY
// - HI_BLUE_ONLY
#define CHECK_HI_C1_ONLY(COLOR1UPPER, colorDiceVal)                                                                          \
              actionReward[HI_##COLOR1UPPER##_ONLY] = -1e9;                                                                  \
              if (canTakeMark(&state, COLOR1UPPER, w2+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))       \
              {                                                                                                              \
                actionReward[HI_##COLOR1UPPER##_ONLY] = newStateW;                                                           \
              }                                                                                                              \
              else if (canTakeMark(&state, COLOR1UPPER, w1+colorDiceVal, p, RYGB, &newStateIx, newColorStates, &newStateW))  \
              {                                                                                                              \
                actionReward[HI_##COLOR1UPPER##_ONLY] = newStateW;                                                           \
              }

// CHECK_W_AS_C1_THEN_C2_LOW:
// This macro checks to see if you can take Choice 1 (C1) (two whites marked as
// a color) followed by Choice 2 (C2), where C2 is the lower of the two
// possible sums (w1+colorDiceVal vs w2+colorDiceVal). If the lower sum is
// unable to be played, evaluate the higher sum. This populates actionReward
// for:
// - WHITE_AS_RED_THEN_LOW_RED   , WHITE_AS_RED_THEN_LOW_YELLOW   , WHITE_AS_RED_THEN_LOW_GREEN   , WHITE_AS_RED_THEN_LOW_BLUE
// - WHITE_AS_YELLOW_THEN_LOW_RED, WHITE_AS_YELLOW_THEN_LOW_YELLOW, WHITE_AS_YELLOW_THEN_LOW_GREEN, WHITE_AS_YELLOW_THEN_LOW_BLUE
// - WHITE_AS_GREEN_THEN_LOW_RED , WHITE_AS_GREEN_THEN_LOW_YELLOW , WHITE_AS_GREEN_THEN_LOW_GREEN , WHITE_AS_GREEN_THEN_LOW_BLUE
// - WHITE_AS_BLUE_THEN_LOW_RED  , WHITE_AS_BLUE_THEN_LOW_YELLOW  , WHITE_AS_BLUE_THEN_LOW_GREEN  , WHITE_AS_BLUE_THEN_LOW_BLUE
#define CHECK_W_AS_C1_THEN_C2_LOW(COLOR1UPPER, COLOR2UPPER, colorDiceVal)                                                    \
              actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = -1e9;                                          \
              if (canTakeWas##COLOR1UPPER)                                                                                   \
              {                                                                                                              \
                if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w1+colorDiceVal, p,                           \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))                            \
                {                                                                                                            \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = newStateW;                                 \
                }                                                                                                            \
                else if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w2+colorDiceVal, p,                      \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))                            \
                {                                                                                                            \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_LOW_##COLOR2UPPER] = newStateW;                                 \
                }                                                                                                            \
              }

// CHECK_W_AS_C1_THEN_C2_HI:
// Same as CHECK_W_AS_C1_THEN_C2_LOW, except it evaluates w2+colorDiceVal first
// and w1+colorDiceVal second. This populates actionReward for:
// - WHITE_AS_RED_THEN_HI_RED   , WHITE_AS_RED_THEN_HI_YELLOW   , WHITE_AS_RED_THEN_HI_GREEN   , WHITE_AS_RED_THEN_HI_BLUE
// - WHITE_AS_YELLOW_THEN_HI_RED, WHITE_AS_YELLOW_THEN_HI_YELLOW, WHITE_AS_YELLOW_THEN_HI_GREEN, WHITE_AS_YELLOW_THEN_HI_BLUE
// - WHITE_AS_GREEN_THEN_HI_RED , WHITE_AS_GREEN_THEN_HI_YELLOW , WHITE_AS_GREEN_THEN_HI_GREEN , WHITE_AS_GREEN_THEN_HI_BLUE
// - WHITE_AS_BLUE_THEN_HI_RED  , WHITE_AS_BLUE_THEN_HI_YELLOW  , WHITE_AS_BLUE_THEN_HI_GREEN  , WHITE_AS_BLUE_THEN_HI_BLUE
#define CHECK_W_AS_C1_THEN_C2_HI(COLOR1UPPER, COLOR2UPPER, colorDiceVal)                                                     \
              actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = -1e9;                                           \
              if (canTakeWas##COLOR1UPPER)                                                                                   \
              {                                                                                                              \
                if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w2+colorDiceVal, p,                           \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))                            \
                {                                                                                                            \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = newStateW;                                  \
                }                                                                                                            \
                else if (canTakeMark(&newTmpStateTookWas##COLOR1UPPER, COLOR2UPPER, w1+colorDiceVal, p,                      \
                    newTmpColStateTookWas##COLOR1UPPER, &newStateIx, newColorStates, &newStateW))                            \
                {                                                                                                            \
                  actionReward[WHITE_AS_##COLOR1UPPER##_THEN_HI_##COLOR2UPPER] = newStateW;                                  \
                }                                                                                                            \
              }

              // Evaluate all the options that just include the white dice and the red die
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
                // Evaluate all the options that just include the white dice and the yellow die
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
                  // Evaluate all the options that just include the white dice and the green die
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
                    // Evaluate all the options that just include the white dice and the blue die

                    // Note that since this is the inner-most loop, this is
                    // where a vast majority of the program's time is spent.
                    CHECK_HI_C1_ONLY(BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(RED   , BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(YELLOW, BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(GREEN , BLUE, b)
                    CHECK_W_AS_C1_THEN_C2_HI(BLUE  , BLUE, b)
                    // End of section where vast majority of the program's time is spent

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
    if ((NUM_MARKOV_STATES - s) % 1000 == 0)
    {
      printf("% 8d / % 8d states complete\n", NUM_MARKOV_STATES - s, NUM_MARKOV_STATES);
    }
  } // end s loop
} // end calculateWvec()

int main(int argc, char *argv[])
{
    FILE *fp = NULL;
    char filenameBuf[128];
    int num_iterations = -1; // -1 means do all of them

    initLookupTables();

    Wvec = malloc(sizeof(float)*NUM_MARKOV_STATES);
    int i;
    for (i = 0; i < NUM_MARKOV_STATES; i++)
    {
      // Initialize to "end of game" values
      Wvec[i] = WVEC_END_OF_GAME;
    }

    if (argc > 1)
    {
      num_iterations = atoi(argv[1]);
      printf("Overriding num_iterations for all to %d. Presumably this is a short timing run?\n", num_iterations);
    }

    calculateWvec(num_iterations);

    snprintf(filenameBuf, sizeof(filenameBuf), "qwixx.bin");
    printf("Saving results to %s ...\n", filenameBuf);
    fp = fopen(filenameBuf,"wb");
    if (fp)
    {
      fwrite(Wvec, sizeof(float)*NUM_MARKOV_STATES, 1, fp);
      fclose(fp);
    }

    printf("Wvec[0] = %.2f\n", Wvec[0]);

    return 0;
}
