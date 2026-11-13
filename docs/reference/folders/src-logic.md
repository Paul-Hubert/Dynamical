# src/logic

The simulation library (`dynamical_sim`): everything that runs without a window or a GPU. It has no files of its own, only subfolders.

| Folder | What |
|---|---|
| [agents](src-logic-agents.md) | the physical walker (HumanBot), IK and the kinematic gait, the sphere and pursuit fixtures, ground fields, walk routes |
| [animals](src-logic-animals.md) | animal library, animation, procedural gait, herds, steering, the physical animal agents |
| [components](src-logic-components.md) | shared plain data components: Transform, RigidBody, Camera, VR, RenderSettings, lights |
| [grass](src-logic-grass.md) | grass baking, species, height and colour fields (CPU side of the grass) |
| [lighting](src-logic-lighting.md) | probe bricks and the CPU mirrors of the lighting |
| [player](src-logic-player.md) | physical players, input devices, holding, grabbing, riding |
| [sandbox](src-logic-sandbox.md) | the action agents' world: rules of life, rewards, minds |
| [settings](src-logic-settings.md) | Settings (config file) and graphics levels |
| [sky](src-logic-sky.md) | sun, moon, sky light, torches |
| [systems](src-logic-systems.md) | `System`, `SystemSet`, chunk systems, box drop |
| [terrain](src-logic-terrain.md) | chunks, terrain generator, CPU marching cubes, soil, ground relief and curvature |
| [vegetation](src-logic-vegetation.md) | SpeedTree library, forests, scatter and lineup |
