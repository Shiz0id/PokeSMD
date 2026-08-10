# Pathfinder 1.1.2

## About

This is a complete reimplementation of [Juanjo's old path finder](https://www.pokecommunity.com/threads/applymovement-vs-a-algorithm-for-pathfinding.445293/) for [`pokeemerald-expansion`](https://github.com/rh-hideout/pokeemerald-expansion) `1.13.0+`.
This branch adds the `moveobjecttocoords` and `approachobject` scripting macros, for generating movement scripts at run time, achieved by using the Weighted A* Algorithm. This branch aims to give romhackers an easier way to create more complex movement scripts and reduce branching.

![demo1](https://github.com/user-attachments/assets/5285b7d1-2e55-4afa-95e2-09312e922544)
![demo2](https://github.com/user-attachments/assets/febabae1-9816-4cd8-96e4-ee33bd0ba198)
![demo3](https://github.com/user-attachments/assets/fcbd8e82-cb36-4905-9237-f1822bd8c7ce)
> Stairs tiles by Zeo254.

## Usage

### `moveobjecttocoords`
```gas
moveobjecttocoords localId:req, x:req, y:req, facingDirection:req, speed=1, maxnodes=256
```

- `facingDirection`, it controls the facing direction. If it is `DIR_NONE`, the facing direction will depend on the generated script.
- `speed`, it controls the movement speed and goes from 0 to 4. 0 is slow, 1 is normal, and so on.
- `maxnodes`, it controls how many nodes can be generated (aka, how many tiles can be checked).

### `approachobject`
```gas
approachobject localId:req, targetLocalId:req, facingDirection:req, approachDirection:req, speed=1, maxnodes=256
```
Same as `moveobjecttocoords`, but keep in mind that `approachDirection` does not support `DIR_NONE`.

### What happens if there is no path?

The system will display an X icon, if it's unable to find a valid path.

![fail](https://github.com/user-attachments/assets/c2d07d42-3308-4e81-9a7f-679c1d4aaa49)

> [!WARNING]
If there is no path, it will most likely lag the game.
Try to only use the macro when you are sure there is a valid path.

## Why Weighted A*?

This variant of the A* algorithm provides better performance for searching the first valid path. This done by giving more priority to nodes more close to the goal.

### A* vs Weighted A*

| A* Algorithm  | Weighted A* Algorithm  |
| ------------- | ---------------------- |
| ![a_star](https://github.com/user-attachments/assets/f3fa2ccc-4074-40da-b03d-d64ccfe83d9e) | ![weighted_a_star](https://github.com/user-attachments/assets/e127af4b-447a-45f2-9ec0-c5aab8965fd7) |

However Weighted A* can generate less natural-looking paths than regual A*, and it can be tricked more easly by mazes, but it provides better performance in most cases, since it requires less nodes to find a valid path.

## Changelog

### 1.1.2 - 2026-6-20
- Updated to `expansion` 1.16.1.

### 1.1.1 - 2026-4-14

### Changed
- Improve preformance by caching metatile behaviors.
- Fix potential UBs.
- Fix hash function `fmix32` implementation.

### 1.1.0 - 2026-1-20

#### Added
- Add `approachobject` macro.
- Add `SLOW_MOVEMENT_ON_STAIRS` support .
- Add `PATH_FINDER_PRINT_TIME`.

#### Changed
- Improve neighbor expansion, about a ~25% faster.

## Credits
- Juanjo, for the orginal path planner idea.
- Phantonomy, for the `approachobject` idea.

## Reference

### Tiles
- [https://www.deviantart.com/zeo254/art/Tileset-Pokemon-Resurgent-495764643](https://www.deviantart.com/zeo254/art/Tileset-Pokemon-Resurgent-495764643)

### Videos
- [https://upload.wikimedia.org/wikipedia/commons/5/5d/Astar_progress_animation.gif](https://upload.wikimedia.org/wikipedia/commons/5/5d/Astar_progress_animation.gif)
- [https://upload.wikimedia.org/wikipedia/commons/8/85/Weighted_A_star_with_eps_5.gif](https://upload.wikimedia.org/wikipedia/commons/8/85/Weighted_A_star_with_eps_5.gif)
