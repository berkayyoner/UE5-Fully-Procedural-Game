# ANALYSIS – Project Roadmap

Source of truth: `RULES.md`. "Creatures" = NPCs and players. Player and humanoid NPCs share the same movement and attack bases (low dependencies, weight/size affect movement).

Project start date: November 5, 2026 (see `DONE.md`).

---

## Phase 0 – Foundation & Project Setup
**Goal:** Stable base that every later phase builds on.
- Confirm Unreal Engine version (latest), project structure, folder/naming conventions.
- Set up `.gitignore` (exclude `Intermediate/`, `Saved/`, `Binaries/`, `DerivedDataCache/`) and Git LFS for binary assets.
- Current state: project is Blueprint-only (no `Aedir/Source`). Decide the path before Phase 1:
  - **Option A – stay Blueprint-only:** no C++ toolchain needed, but `.uasset` files cannot be diffed/merged or edited outside the editor. Work would go through the editor (Python Editor Script / Remote Control) or manual editing.
  - **Option B – hybrid (recommended):** add a C++ module and move performance-critical and shared logic (procedural locomotion, balance, stats, replication) to C++; keep Blueprints for data, tuning and content. Existing Blueprints (e.g. `BP_PlayerCharacter`) can be reparented to the C++ classes.
  - Decision is pending; phases below are written to work with either, but Phases 2, 3 and 10 (physics, procedural IK, replication) benefit most from C++.
- Reuse the user's Unreal Engine library (audit what exists, document what is reused).
- Module layout with minimal dependencies (Core Creature module, Items, World, UI, Net).
- Co-op readiness rule: all gameplay code written replication-aware from day one (server authority, client prediction).
- Rendering baseline: Nanite, Lumen, Virtual Shadow Maps; texture streaming; max texture size 4K policy.

**Exit criteria:** Project builds from command line, repo is clean, module skeleton compiles.

---

## Phase 1 – Core Creature Framework
**Goal:** Shared base for player and NPCs.
- `Creature` base class (humanoid), shared by player and NPC; controller abstraction (Player input vs AI).
- Stats component: max/current health, mana, stamina, strength, athletism, and skills (single-hand, two-hand, ranged, destruction, regeneration, illusion, metamorphosis, summon, heavy armor, light armor, smelting, alchemy, enchanting).
- Dynamic stats: stamina drain from running/swinging, slow max-stamina growth; NPCs have dynamic stats too.
- Strength affects model height/size and carry/swing capacity; athletism affects speed, jump height, fall control.
- Group/faction system: group assignment, relation matrix (enemy / neutral / ally) and queries.
- Data-driven definitions (DataAssets/DataTables) for stats and groups.

**Exit criteria:** A creature can be spawned with stats and a group; relation queries work; replicated.

---

## Phase 2 – Procedural Physics Body & Balance
**Goal:** Ragdoll-based humanoid held up by the pelvis.
- Physics Asset setup: all bodies simulated except the pelvis.
- Pelvis controller: holds body up, position/rotation driven by balance state.
- Balance model: balance value, recovery, loss thresholds; pelvis leans opposite to loss direction for visible stumbling.
- Fall logic: if balance is not recovered within a time window, creature falls (ragdoll).
- Damage/balance events: fall height, wall impact, thrown/hitting objects reduce balance and health (athletism reduces fall damage).
- Get-up behavior after falling.
- Breathing and idle sway while alive.

**Exit criteria:** Character stands, sways, loses balance when pushed, and falls/recovers believably.

---

## Phase 3 – Procedural Locomotion (no animation assets)
**Goal:** All movement computed procedurally (IK / physics driven).
- Foot placement: step planning, step length/height/speed, foot IK against terrain.
- Walk (W/A/S/D), run (L-Shift), crouch (L-Ctrl), jump (Space) with stamina cost.
- Directional speed multipliers: forward (walk/run) faster than sideways and backward.
- Terrain/stair detection: ground-height traces, adapt step height, length, speed; dynamic hip height.
- Arm swing, spine/pelvis counter-rotation, procedural gait scoring (builds on existing step-scoring work).
- Look system: head-neck-spine distribution within an angle; beyond the angle, lower body turns with 2 in-place steps when standing; when moving, body turns toward look angle with visible head-neck lead.
- Surface-aware friction (hooks into Phase 6).
- Landing handling based on fall height and athletism.

**Exit criteria:** Smooth, realistic walk/run/crouch/jump on slopes and stairs with no animation clips.

---

## Phase 4 – Items, Inventory & Interaction
**Goal:** Everything pickable; hands, waist and drop/use flow.
- Item classes: Weapon, Unusable, Poison, Book, Armor, Object (non-pocketable).
- Pickup detection range + highlight glow on looked-at pickable items (player).
- Procedural reach: Q/E per hand, knee bend via pelvis height, hand reaches item, attaches when close; 2-second reach timeout cancels.
- Hold points per item (predefined sockets, default center); two-handed holds.
- Drop: Q/E with item in that hand drops it; usable items used by holding Q/E.
- Waist slots: T/Y attach/detach per hand; 6 slots (3 left-back, 3 right-back).
- Weight affects reach, swing and movement.
- NPC use of the same interaction API (AI-driven).

**Exit criteria:** Player and NPC can pick up, hold, drop, use and holster items.

---

## Phase 5 – Combat
**Goal:** Fighting stance and procedural attacks.
- Fighting stance (hold LMB/RMB): knees bent, wider legs, one foot forward, walk-only.
- Unarmed: fists up; player raises only the controlled arm (LMB left, RMB right); hand and fingers form a fist.
- Mouse-driven arm swings for the player (constrained swing arcs to stay realistic).
- NPC attack logic: distance to enemy, line-of-sight check, turn toward visible enemies, swing arms.
- Weapon swinging for held items; weight/strength determine speed and power.
- Hit detection, damage, balance impact, stamina cost; hit reactions via physics.
- Group-based target selection (uses Phase 1 relations).

**Exit criteria:** Player and NPCs can fight unarmed and with weapons; hits affect health and balance.

---

## Phase 6 – Surfaces & Footsteps
**Goal:** Physical materials that affect gameplay.
- Surface types (stone, grass, mud, wood, metal, snow, ice, sand, wet variants, etc.) as physical materials.
- Per-surface friction/slipperiness affecting creatures and physics objects.
- Footstep effects (sound, particles, decals) by surface.
- Wetness/snow state layer on surfaces (runtime-painted mask) for Phase 7.

**Exit criteria:** Different surfaces change movement and produce distinct footstep effects.

---

## Phase 7 – Weather System
**Goal:** Detailed weather that physically affects the world.
- States: sunny, extra sunny, cloudy, rainy, stormy, thunderstorm, snowy; smooth transitions.
- Sky/atmosphere, clouds, lighting, fog per state (Lumen compatible).
- Niagara particles: rain (drops that bounce), snow, lightning; realistic and optimized.
- Surface response: open surfaces get wet or covered with snow; wetness and snow persist for a while then dry/melt.
- Footprint wetness transfer: wet-to-dry stepping spreads wetness for a few steps; effects persist for a time.
- Weather affects friction (hooks into Phase 6), sound, and exposure (open vs covered areas detection).
- Replicated weather state for Co-op.

**Exit criteria:** Weather changes visuals and surface state; wet footprints visible and persisting.

---

## Phase 8 – UI / HUD
**Goal:** Gameplay-facing UI.
- Crosshair at the screen center.
- Compass at top: cardinal directions, area icons, mission markers.
- Health (left), mana (middle), stamina (right) bars at the bottom; fade in when below current maximum.
- Item highlight/prompt integration (Phase 4).

**Exit criteria:** HUD reflects live stats and markers.

---

## Phase 9 – World, Terrain & Rendering Optimization
**Goal:** Realistic but optimized visuals.
- Nanite landscape/meshes with height-map displacement so terrain is not flat.
- Texture policy: compress anything above 4K down to 4K using Unreal's texture settings (Max Texture Size / LOD bias), enforced via an editor validation/commandlet.
- Upscaler support: latest DLSS, FSR and XeSS compatible with the engine version (plugins, settings menu).
- Profiling pass: CPU/GPU budgets, LODs, streaming, particle budgets.

**Exit criteria:** Target FPS met on reference hardware with upscalers selectable.

---

## Phase 10 – Multiplayer / Co-op
**Goal:** Single player or Co-op.
- Replicate creature state, procedural physics, items, weather, surfaces.
- Authority model for physics-driven balance and hits; client prediction/smoothing.
- Session handling (host/join), player spawn, save/load of shared world state.

**Exit criteria:** Two or more players can play together without desync issues in core systems.

---

## Phase 11 – Polish, QA & Packaging
- Gameplay tuning (balance values, stats curves, stamina rates).
- Automated tests for stats, group relations, item flow; soak tests for physics stability.
- Bug fixing, memory and load-time optimization.
- Packaging pipeline and build verification.

---

## Phase 12 – Half Sword-style Physical Combat (added after RULES "Design References")
**Goal:** Melee that is controlled by the mouse and decided by physics, not by canned swings.
- 12.1 Mouse-driven arm/weapon control: the hand target follows the mouse around the shoulder with a spring-damper, so weapons have inertia; speed is limited by strength, stamina and weapon weight.
- 12.2 Impact damage model: damage from kinetic energy (mass and speed of the weapon or fist at contact), hit zones from the struck bone (head, torso, arms, legs), damage types (blunt, slash, pierce), balance impact from the same impulse.
- 12.3 Physical blocking and parrying: weapon-versus-weapon contact absorbs momentum, drains stamina and staggers both fighters.
- 12.4 Armor coverage per body part and limb injury (injured arm = weaker grip and slower swings, injured leg = limp and worse balance).
- 12.5 Two-handed grip, shoving, kicks, grabbing.
- 12.6 Weapon catalog with mass, length, center of mass and damage profile (sword, axe, mace, spear, dagger).
**Exit criteria:** A fight between two creatures is decided by timing, positioning, weight and stamina; swing damage differs with real speed and hit location.

---

## Phase 13 – Realistic Locomotion Upgrade
**Goal:** Movement more realistic than Half Sword.
- Balance from the center of mass over the support polygon, weight shift between feet, turning and acceleration inertia.
- Resting arm pose, breathing and idle sway (fixes the A-pose arms), crouch and turn-in-place polish.
- Drive the simulated limbs with physical motors toward the procedural pose (PhysicalAnimation or PhysicsControl) so collisions push limbs.
- Stumble and recovery reactions, protective arm reflex when falling, get-up sequence.
**Exit criteria:** Walking, running, stopping and falling look physically plausible from the third-person camera.

---

## Phase 14 – Elder Scrolls-style UI
- List-based menus (inventory, magic, skills, map, journal), Skyrim-like layout, typography and sounds, built in code (UUserWidget trees) so no editor assets are needed.
- Skill level-up feedback, dialogue UI, quest journal and compass quest markers, character creation (race, appearance, starting skills).

## Phase 15 – Elder Scrolls-style Content (follows Game Lore.docx)
- Races (Elves, Dark Elves, Dwarves, Humans), factions and guilds, skill progression tied to the existing skill stats, books and lore items, quests, locations, NPC routines.

---

## Dependency Overview
Phase 0 → 1 → 2 → 3 → (4, 5) ; 6 → 7 ; 1 → 8 ; 3/6 → 9 ; all → 10 → 11. After the RULES update: 5 → 12 ; 3 → 13 ; 8 → 14 → 15.

## Key Risks
- Pelvis-only simulated ragdoll stability (tuning, solver iterations).
- Animation-free locomotion realism (IK + physics tuning time).
- Persistent runtime surface wetness/snow cost (use low-res render-target masks / virtual textures).
- Co-op replication of physics-driven characters.
- Project is currently Blueprint-only: binary assets limit code review and external editing; per-frame procedural IK/balance in Blueprint may hit performance limits (consider Nativization-style migration to C++ for hot paths).
