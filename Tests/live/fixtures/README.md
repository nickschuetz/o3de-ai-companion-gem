# Live test fixtures

Assets the live editor suite (`Tests/live/test_live_editor.py`) copies into
the host project while it runs. They live here, not under `Assets/`, so that
enabling the gem does not make every project's AssetProcessor build them.

## AiCompanionSample.animgraph

An EMotion FX anim graph (ObjectStream XML) for the `list_anim_graphs` and
`get_anim_graph` tests. It was pruned from the engine's
`Gems/EMotionFX/Code/Tests/TestAssets/EMotionFXBuilderTestAssets/AnimGraphExample.animgraph`
test asset (o3de/o3de, Apache-2.0 OR MIT, the same license as this gem) with a
script that kept:

- the root state machine `Root`, with two motion nodes as its only children:
  `Idle` (id 16013080886305186473, motion `jack_idle_zup`) and `WalkForward`
  (id 18032911468787307812, motion `jack_walk_forward_zup`); `Idle` is the
  entry state
- one state transition (id 14485842391560844526) from `Idle` to `WalkForward`,
  blend time 0.3 s, with its `AnimGraphParameterCondition` on the `Speed`
  parameter
- one value parameter, `Speed`, a float slider

Everything else was dropped: the nested state machines and blend trees, the
other nine motion nodes, the second transition, the other four parameters, the
node groups and the game-controller presets. The motion ids refer to motions
in the engine test asset's motion set, which the tests do not load; the graph
is only read, never activated on an actor.

`scripts/ci_live_test.sh` copies the file to
`<project>/Assets/AiCompanionLiveTest/AiCompanionSample.animgraph` before
starting AssetProcessor and removes it on exit; the test class copies it
itself when it is missing, so the suite also works against an editor started
by hand with `AICOMPANION_PROJECT` set.
