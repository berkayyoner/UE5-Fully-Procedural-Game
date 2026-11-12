**Game Engine:** Unreal Engine (Latest)

***Creatures means NPCs and players.***

## Mechanics
* Third-person view. Also supports first-person view secondary.
* Physics based fully procedural medival fantasy RPG game.

## Design References
* **Gameplay and mechanics: Half Sword style.** Fully physics-driven characters and melee (no canned animations). The player moves the weapon/arm with the mouse; weapons have weight and momentum; damage comes from real impact (speed, mass, contact point), not from a fixed number. Hit location matters (head, torso, arms, legs), armor covers body parts, limbs can be injured, stamina limits arm strength, and blocking/parrying happens through physical weapon contact. Character movement should be even more realistic than Half Sword: weight shift, momentum, foot placement, balance from the center of mass, stumbling, protective reactions when falling. Inspiration: Half Sword.
* **UI and game content: The Elder Scrolls style (Skyrim/Oblivion).** Minimal HUD (compass on top, three bars at the bottom), list-based menus (inventory, magic, skills, map, journal), skill-based progression, races, factions and guilds, books and lore items, quests with compass markers. World lore is in `Game Lore.docx` (the planet Aedir, the Anga gods, Elves, Dark Elves, Dwarves and so on) and content must stay consistent with it. Inspiration: The Elder Scrolls.

## Expectations
* Player character will be a humanouid type. So body physics will be enabled except Pelvis bone. Pelvis hold the body up and will be simulated depending on the balance. When character losts balance, pelvis should rotate to opposite way to show the player like it's loosing balance visually and when character can't get it's balance back for a while character should fall. Falling from a high place or hitting a wall or get hit by an item/object should effect balance and health.
* Humanoids should be able to walk (W,A,S,D for player), run (L-Shift for player), crouch (L-Ctrl for player), jump (Space for player), breath and sway if alive. They should look around with head-neck-spine in an angle if they are standing, if they need to exceed the angle for willing to look an angle, their lower body should turn there and they should take 2 steps in the same place to turn. If they are moving, body should turn to the looking angle but there should still be a visible head-neck turn.
* All creature movements will be calculated without an animation and all will be procedural and should look realistic.
* Creatures should detect stairs and ground heights and adjust their step height, step length and step speeds.
* Walking and especially running forward should be faster than going sideways and back like in real.
* There will be detailed weather system. Weathers will be sunny, extrasunny, cloudy, extrasunny, rainy, stormy, thunderstorm, snowy. Particle effects has to be as realistic as possible. Weather should effect the surfaces. For example; rain should drop and bounce off the ground, open surfaces should get wet or snow. When a creature steps on a wet surface and takes a few step in dry areas, it should get the dry ares wet. Surface effects should stay for a while.
* There should be different surface types which effects the physic object and creatures on it. For example different slipperyness for different surfaces.
* Creatures should have a fighting stance. Player should enable fighting stance by holding right or left mouse buttons. So they should bend the knees a bit more, keep the legs a bit more apart from the normal, always keep an foot further forward than the other, walk only. If there is no equiped/held weapon, fists should keep high as ready to swing. If it's players, only controled arm should get raised dependig on which mouse button is pressed. Left MB for left arm, right MB for right arm. Hands and fingers should get a fist shape.
* There will be group system which decides which creature is in which group and that will decide if they are enemies, natural or allies with the other creatures arround.
* Humanoid creatures swing fists controlled depending on the distance with enemy and they will turn to enemy if they CAN see them. Players will swing controled arms depending on mouse move. Player will swing the mouse and arms will simulate it. Fist swinging should be in a setain angle to look realistic instead of random arm swinging. If creatures grab or has an item in their hand, they should swing the item.
* Items should have different classes which will decide if they are weapon, unusable, poision, book, armor, object (objects which can not fit in backpack in the future).
* If player has any item type or weapon in hand except object, items will be held from the places which will be predecided in development. Default pick point will be center.
* All creatures will have max health, mana, stamina, strength (which effects model height and size), atletism (for speed and jump height), single hand weapon, two handed weapon, ranged weapon, destruction magic, regeneration magic, illusion magic, metamorphosis magic, summon magic, heavy armor, light armor, smelting, alchemy, enchanting skill stats.
* All items should be pickable by creatures. If player looking at a item and the item is in a pickable range, item should have a small UI glow around it the be able to understand and see easier for player. Creatures should reach for the object if they will take it and should bend the knees by getting pelvis to right height to get lower to grab the object. If object gets so close to the hand which is used for grabbing, it should attach the hand. Player will use Q and E keys for each hand to start reaching. If creatures reaches and can't grab the object for 2 seconds, it should cancel reaching. If there is a item in hand of a player, when player uses Q or E, it should drop the item in the pressed hand. If it's a usable item, player should be able to use the item by holding Q and E keys. NPCs should do all of these interactions too.
* Player should attach the items in hand to it waist by pressing T and Y depending on hand. Waist should be able to hold 6 items. 3 on left side to back, 3 on right side to back.
* Running or swinging to much should lower stamina but increase max stamina slowly. NPCs will have dynamic stats too.
* Strength should get model size and height bigger than others and creatures should be able to swing and raise heavier items. More athetism should increase running speed, jump height and falling control. So players will hit the ground from falling will depend on height and stat.
* Foot steps will make effects depending on the surface material.

## UI
* There will be a crosshair in the middle of the screen for better aimming.
* There will be a compass on top which shows poles, area icons and mission markers.
* There will be health, mana, stamina bars which appears with fade in when they go lower then current maximum. They will be on left, middle and right of the buttom.

## Important
* Keep the dependancies low. Humanoid NPCs and player character will be using same movement and attack bases. Weight and sizes will effect movements.
* Project should support latest versions of DLSS, FSR and XeSS which supports current used Unreal Engine version.
* Nanite will be used and height map will be added to meshes. So landscape texture will not be flat.
* Use my Unreal Engine library.
* It will be a realistic game but should be as optimized as possible. So try to compress textures which are above 4K down to 4K by using Unreal Engine's feature.
* This will be a game which can be played single player or Co-op.
* Make sure blueprints are clean for easy to read.
## Player Requests (gathered from play tests)
### Body and physics
* The body is a physics simulation, never an animation: it sways a little, carries weight, has momentum and inertia, and nothing passes through it, not even what it holds itself.
* What is held is felt through real physics (its mass pulling on the hand), never by making the controls less precise. A two-handed weapon in one hand is hard to carry and swing.
* Weapons collide with bodies and with each other. A blow lands, stops or glances; it never cuts the air through a body. How deep an edge cuts depends on its speed: too weak it glances or bounces, in between it bites and sticks, strong enough it goes through.
* Being hit is felt by strength and direction: the struck part and the trunk move, balance is lost in proportion, a body that cannot recover stumbles and falls, and gets up again.
* Nobody dies suddenly. Unless the head is cut off, death comes slowly, through wounds, bleeding, falling, failing to get up.
* Limbs can be cut. A creature that loses its weapon arm drops the weapon; unarmed or without both arms it flees.
* Bodies, living or dead, standing or lying, and severed limbs can be grabbed and dragged; every part has a weight.
* Downward blows reach the ground so a lying enemy can be hit; the spine bends with vertical swings.
* Sword-like weapons are held more upright. Half-swording keeps the upper hand fixed on the blade; it lets go only under real strain, and a weapon can be wrenched from the hand.
* When running, loose arms swing moderately with the elbows only lightly bent.
* Swings accelerate and decelerate, they never move at a constant speed.
### Controls
* F talks to NPCs or points at a spot or item for orders. G shoves. T/Y stow; Q/E reach, drop and, held, use. Holding the use key eats or drinks with the right angle at the mouth. Shift runs, also in the fight stance.
* A big object (boulder) is let go with both hands at once.
### NPCs
* NPCs have health, mana and stamina with the same limits as the player; the player does not see enemy mana and stamina.
* NPCs pick up and wear gear they lack, carry weapons on the belt outside fights and draw them when threatened; spears and staves do not go on the belt.
* Orders: follow, go there (with a marker), fetch the pointed-at item and wear it.
* Talking works as in Skyrim: the NPC turns to the player, the world stops, the NPC's head moves as it speaks. The dialogue screen looks like Skyrim's (see Skyrim references), and "Farewell" ends it.
* NPCs say things to the player outside dialogue and hold long conversations with each other, shown as Skyrim-style subtitles.
### World and look
* The test level is a natural landscape with a master material made from all the Megascans surfaces, with height maps, non-repeating and at realistic scale; full of nature, decor and test-ready items (food and drink on tables).
* Grass is realistic and calm, not tall or overly animated. Rain drops are small and splash on the ground; weather has sounds.
* Blood stays on the ground for three game days. Footprints have the shape of a real shoe sole; footstep sounds match the ground and are not too loud; footstep effects are subtle and grow with step speed.
* Props have collision. Weapons are of realistic size. Items drawn from the belt visibly come from the belt.
### Tools
* A Skyrim-style console (the " key) with base and reference ids, click to select, history with up/down, suggestions, help, player.additem/removeitem, spawn/placeatme, enable/disable, delete, kill, moveto, getpos, setscale, player.setstats/addstats/removestats, setweather and more.
* Use the Fab library generously: add whatever makes the game better.
