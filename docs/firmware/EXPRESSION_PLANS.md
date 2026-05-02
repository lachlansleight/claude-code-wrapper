OK so the list of states and the things that used to trigger them is as follows:

Sleep when we boot up.

Session start triggers a Wake Up.
Wake up always leads to Excited after its 1 second animation plays.

Excited decays to Ready after 10 seconds
Ready decays to Idle after 60 seconds
Idle decays to Sleep after 30 minutes

Or if a message comes in, we enter Thinking.
When reading starts, we enter Reading. When it ends, we return to Thinking.
When writing starts, we enter Writing. When it ends, we return to Thinking.
When executing starts, we enter Executing. When it ends, we return to Thinking.
However if executing continues for 5 seconds, we enter executing_long.
If that continues for 30 seconds, we enter blocked.

Once the turn ends, we enter finished.
Finished always leads to Excited after its 1.5 seconds animation plays.

So the Expression triggers we actually want for each of these are:

AWAKEN          Session start triggers a Wake Up.
                Wake up always leads to Excited after its 1 second animation plays.

SETTLE          Excited decays to Ready after 10 seconds
RELAX           Ready decays to Idle after 60 seconds
SLUMBER         Idle decays to Sleep after 30 minutes

PONDER          Or if a message comes in, we enter Thinking.
READ            When reading starts, we enter Reading. When it ends, we return to Thinking.
WRITE           When writing starts, we enter Writing. When it ends, we return to Thinking.
FOCUS           When executing starts, we enter Executing. When it ends, we return to Thinking.
EXERT           However if executing continues for 5 seconds, we enter executing_long.
REPENT          If that continues for 30 seconds, we enter blocked.

CELEBRATE       Once the turn ends, we enter finished.
                Finished always leads to Excited after its 1.5 seconds animation plays.

---

As a side-note, I'd like in Parabrain to try to generate some immediate oscillation parameters that we could use to play these expressions while we wait for the actual full response and TTS to generate.

---