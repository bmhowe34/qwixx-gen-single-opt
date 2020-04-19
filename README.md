# qwixx-gen-single-opt
Generate the optimal single player Qwixx strategy

This is a program that corresponds to the reddit post:
https://www.reddit.com/r/boardgames/comments/6h4qpi/solving_single_player_qwixx/

Writeup: https://drive.google.com/open?id=0B0E4VFlFjnCuME9sZGhrbGRIWXc

Originally, it was written to be multi-threaded and run many iterations. It took many hours of compute time (detailed in paper). /u/chaotic_iak helped me discover that the problem could be done in fewer iterations, so the next version includes an update to run in only a single iteration. It currently takes about 90 minutes to run.
