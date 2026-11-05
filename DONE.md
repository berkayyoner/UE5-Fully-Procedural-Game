Project Start Date: November 5, 2026
Phase 0 (partial): added .gitignore and removed generated Intermediate/Saved/DerivedDataCache files from version control.
Phase 0 (done): hybrid C++/Blueprint chosen; added Aedir C++ primary game module (targets, Build.cs, LogAedir), builds on UE 5.7.
Phase 0 (done): upgraded FSR3 to AMD FSR 4.1.1 (UE 5.7) and moved FSR/DLSS plugins into Aedir/Plugins so C++ builds work.
Phase 1 (done): creature framework - AAedirCreature base, replicated UAedirStatsComponent (health/mana/stamina, strength size scale, athletism, 13 skills, dynamic max stamina), UAedirGroupSubsystem + Aedir Groups settings (enemy/neutral/ally).
Phase 2 (done): UAedirBalanceComponent - pelvis-held ragdoll body, replicated balance with opposite-direction pelvis lean, fall/get-up logic, fall/wall/object impact damage.
Phase 3a (done): predicted AedirCharacterMovementComponent (directional walk/run/crouch/fight speeds, athletism/carry modifiers, stamina cost for run/jump) and AAedirPlayerCharacter with WASD/Shift/Ctrl/Space/mouse input shared creature move API.
Phase 3b (done): UAedirGaitComponent + UAedirProceduralAnimInstance - animation-free procedural gait (ground/stair-aware foot placement and step height/length/speed, dynamic hip height, two-bone leg IK, arm swing, head-neck-spine look, in-place 2-step turning via movement component PhysicsRotation).
Phase 4 (done): AAedirItemActor (6 item classes, hold/waist attach, highlight) + UAedirInteractionComponent (reach with 2s timeout, grab, drop, use-on-hold, 6-slot waist, weight limits, replicated) with Q/E/T/Y player input and procedural reach IK + knee bend in the gait pose.
Phase 5 (done): UAedirCombatComponent - LMB/RMB per-arm fight stance, mouse-driven constrained jab/hook/uppercut/overhand arm swings with held-item swinging, hit/damage/balance impact, NPC sight-based combat AI; stance pose (bent knees, wide legs, lead foot, high guard fists, finger fist curl).
