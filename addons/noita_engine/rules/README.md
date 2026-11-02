# Simulation rules

Every tick, `update_cell()` runs one cell's movement and then its chemistry. Most new
rules are data, not code: add a `PixelMaterial` or `PixelReaction` `.tres` to a
`PixelMaterialSet` and the existing code picks it up. See
`demo/resources/materials/default_set.tres` for the example set and the `.tres` files next
to it for what each material and reaction looks like.

## What each file owns

- `sim_rules.h` — the one entry point the simulator calls: `update_cell()`.
- `movement.cpp` — `update_cell()` and the movement: `passable`, `step`, `run`, `dir_for`,
  `next_speed`, `spread_of`, `move_powder`, `move_liquid`, `move_gas`.
- `chemistry.cpp` — `run_chemistry()` and its stages: `react`, `exchange_heat`,
  `phase_change`, `burn`, `decay`.
- `rules_common.h` — the `Mover` and `Chem` structs and the `run_chemistry()` declaration
  shared by the two files.

## Adding a rule

A powder that turns to liquid next to fire is a reaction: create a `PixelReaction` `.tres`
with `a` the powder's name, `b = &"fire"`, `a_into` the liquid's name, a `probability`, and
add it to the material set's `reactions` array. The C++ function that runs these is
`react()` in `chemistry.cpp` — edit it when the behaviour needs more than a reaction
expresses (e.g. a product that depends on the cell's temperature rather than a fixed
`a_into`).

Rebuild with `../build.sh`.
