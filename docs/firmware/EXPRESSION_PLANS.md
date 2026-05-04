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

An alternate way to read each state as we have it right now:

Idle => Emotion: Neutral
Thinking => Verb: Thinking
Reading => Verb: Reading
Writing => Verb: Writing
Executing => Verb: Executing
Executing_Long => Verb: Straining
Finished => Emotion: Joyful
Excited => Emotion: Excited
Ready => Emotion: Happy
Waking => transition animation from sleeping into any other state
Sleep => Verb: Sleeping
Blocked => Emotion: Sad
WantsAttention => Verb: Attracting Attention

---

So the Expression triggers we actually want for each of these are:

session.started: add activation and valence to lead into excited
session.ended: go to sleep (verb)
turn.started: start thinking (verb)
activity.started: play applicable verb
activity.finished: 1s linger, leading back into thinking verb
turn.ended: add activation and valence to lead into joyful
notification: play attracting attention verb