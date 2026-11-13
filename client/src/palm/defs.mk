# The Palm client's build choices (config.h), shared by src/palm/palm.mk (the Palm) and the Makefile's pc-palm.
PALM_DEFS = -DFANCY_TITLE=1 -DFANCY_PLAYFIELD=1 -DDISPLAY_IDLE=1 -DTITLE_IDLE=1 -DQUIT_TO_OS=1 '-DQUIT_ASK="QUIT? Y/N"' \
            -DREGION_PICK=1 -DLOBBY_KEY=0 -DLOG_HISTORY=16 '-DSAY_MAXBUY="3 PER TURN: TAP BUY"'
