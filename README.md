# Aedir

**A fully procedural, physics-driven medieval fantasy RPG in Unreal Engine 5.**

A solo project, designed, developed and maintained by **Berkay Öner**.

![Unreal Engine 5.8](https://img.shields.io/badge/Unreal%20Engine-5.8-313131?logo=unrealengine)
![C++](https://img.shields.io/badge/Language-C%2B%2B-00599C?logo=cplusplus)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6)
![Status](https://img.shields.io/badge/Status-In%20development-orange)

## Overview

Aedir is a third-person RPG, with a first-person view, whose characters move without canned animations. Every step, stance, blow and fall is worked out at run time from the body's weight, balance and momentum. Combat follows from it: a weapon is moved with the mouse, and what it does depends on how fast it arrives, how heavy it is and where it lands.

## Features

- **Procedural movement.** Walking, running, crouching, jumping, turning and getting up, with steps fitted to slopes and stairs.
- **Physical bodies.** Muscle-driven physics for every character: balance from the center of mass, stumbling, falling and rising again.
- **Physics-based combat.** Fists, kicks, shoves, grabs and weapons, with damage from real impact speed, mass and contact point.
- **Wounds and injuries.** Bleeding, broken limbs and severing by body part, with armor that covers what it covers.
- **Expressive characters.** Facial expressions, eyes that follow what is seen, and hands that go to a hurt place.
- **Living NPCs.** Needs, chatter, conversations, followers, looting and the same fighting body as the player.
- **Dynamic weather.** Seven kinds from clear sky to thunderstorm and snow, with surfaces that react to it.
- **RPG systems.** Stats and skills, items and equipment, map and travel, menus, saving and multiplayer sessions.

## Controls

| Action | Key |
| --- | --- |
| Move | `W` `A` `S` `D` |
| Run / crouch / jump | `Left Shift` / `Left Ctrl` / `Space` |
| Right hand: guard and strike | Hold `Left Mouse` and move the mouse |
| Left hand: guard and strike | Hold `Right Mouse` and move the mouse |
| Left / right hand: take and drop | `Q` / `E` |
| Left / right hand: use | `Y` / `T` |
| Kick / shove | `C` / `G` |
| Thrust / turn weapon / change hands | `Left Alt` / `R` / `X` |
| Lock on | `Caps Lock` |
| Talk | `F` |
| Change view / head camera | `V` / `Z` |
| Menu / journal / map / skills | `Tab` / `J` / `M` / `K` |

## Project Structure

```text
Aedir/
├── Source/Aedir/         Game module
│   ├── Animation/        Procedural gait, posture and face
│   ├── Physics/          Balance, muscles, falling and getting up
│   ├── Combat/ Fists/    Strikes, guards, kicks and impacts
│   ├── Wounds/           Injuries, blood and severing
│   ├── Creature/ Npc/    Characters and their behavior
│   ├── Player/ UI/ HUD/  Input, camera and menus
│   ├── Weather/ Surface/ Weather, footsteps and ground
│   ├── Net/ Travel/      Saving, sessions and travel
│   └── Tests/            Automation tests
├── Source/AedirEditor/   Editor module: import and content tools
└── Content/              Game assets
Scripts/                  Packaging and test scripts
```

## Getting Started

### Requirements

- Windows 10 or 11
- Unreal Engine 5.8
- Visual Studio with the C++ game development workload
- [Git LFS](https://git-lfs.com/)

### Build and Run

1. Install Git LFS and clone the repository.
2. Right-click `Aedir/Aedir.uproject` and choose **Generate Visual Studio project files**.
3. Build the `AedirEditor` target (`Development`, `Win64`).
4. Open `Aedir/Aedir.uproject`.

### Tests

```powershell
./Scripts/RunTests.ps1
```

## Documentation

- [Roadmap](ROADMAP.md): what is planned
- [Changelog](DONE.md): what has been built, line by line
- [Credits](CREDITS.md): third-party assets and their licences

## Maintenance

<details>
<summary>Push commits 5 by 5</summary>

> **Warning:** this force-pushes `main` on the remote.

```powershell
# Get all commit hashes in chronological order (oldest first)
$shas  = @(git rev-list --reverse HEAD)
$step  = 5      # Push every 5th commit as a checkpoint
$total = $shas.Count

# Loop through the commits at the given interval and push each checkpoint
for ($i = $step - 1; $i -lt $total; $i += $step) {
    $sha = $shas[$i]
    Write-Host ""
    Write-Host "=== Push $($i+1)/$total : $sha ===" -ForegroundColor Cyan

    # Push this single commit to the remote 'main' branch (force update)
    git push origin "+$sha`:refs/heads/main" --force

    # If the push fails, stop the loop so you can retry with a smaller step
    if ($LASTEXITCODE -ne 0) {
        Write-Host "!! Push failed. Reduce $step to a smaller number and resume from this point." -ForegroundColor Red
        break
    }
}

# Finally, push all remaining commits up to HEAD
Write-Host ""
Write-Host "=== Final push (remaining commits) ===" -ForegroundColor Cyan
git push origin HEAD:refs/heads/main --force
```

</details>

## Author

**Berkay Öner** · [GitHub](https://github.com/berkayyoner)
