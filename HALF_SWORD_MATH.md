# Half Sword weapon control: analysis and the model to build

Sources: the pose grid, swing sequences and front views recorded in Half Sword (HALF_SWORD_COMBAT.md, "Observed while
playing", "Pose grid", "How the control really works", "Swing sequences"), the user's reference stills 17-57, and the
recorded thrust and pole weapon poses. Half Sword's own code is encrypted; everything here is read off what it does.

## 1. What our game did, and why it cannot match

Our swing took the mouse as an aim angle (X, Y) and looked up a pose for it: a hand position and a blade direction, blended
between 23 key poses read off the stills. A physical arm then tried to reach that pose, and a table of measured corrections
pushed it further where it fell short.

That fails for three reasons that no amount of tuning removes:

1. **The pose is a function of the mouse position; Half Sword's is not.** In Half Sword the mouse moves the *hands*, and the
   blade's attitude follows from where the hands are and where the enemy is (section 3). A table keyed by the mouse puts the
   same blade attitude at a mouse position whatever the hands are doing, so between the stills it shows attitudes Half Sword
   never shows ("waving a stick").
2. **The mouse field was the wrong size.** 450 px of mouse moved our weapon 13 degrees; in Half Sword 450 px takes the hands
   from the middle to the arm's full length, and about 220 px from the hips to above the head.
3. **What was asked was not what was shown.** The physical arm stops short and by how much depends on the way it came, so a
   fixed table of corrections is right for one path and wrong for the next. Measured: the same pose, ref 27, came out level
   once and 70 degrees down the next time.

## 2. What Half Sword does (observed)

- O1. The left button held, mouse deltas move the hands. The target is kept when the button is let go and the next press
  goes on from it; there is no fixed map from mouse position to pose.
- O2. The hands move over a wide field bounded by the arms' reach: 450 px sideways takes them from before the chest to the
  arm straight out at shoulder height; about 220 px up takes them from the hips to above the head.
- O3. In the middle the point is on the enemy. Moving the hands right, the point stays on the enemy as long as it can, so the
  blade crosses before the body to the left; at the far right the arm is straight and the blade stands upright in the hand.
  Moving left, the hands cross over; half way the blade points forward-left; at the far left the blade lies back over the
  left shoulder.
- O4. Up on the left the blade hangs point down before the body (hanging guard); up in the middle it lies level across to
  the left; up on the right it stands upright by the shoulder. Low in the middle the point is low forward; low right the
  blade rises aslant across from the hip; low left it hangs down by the leg.
- O5. With the hands at the edge of their field (by the right shoulder), circling the mouse turns the blade a full smooth
  circle while the hands hardly move: the mouse's movement past the edge steers the blade about the hands.
- O6. Nothing ever jumps: the blade's way changes continuously, wrists and forearms turn with it, and the whole body (chest,
  hips, feet) turns round with the weapon, a quarter turn or more on a far sweep.
- O7. Letting go of the button the weapon returns to the carried pose in about half a second.
- O8. Thrust (Alt): chambered by the right hip, blade level at the waist; out, the arms straight ahead, blade level.

## 3. The model

State, all in the body's own frame (x forward, y right, z up, from the middle of the chest):

- `H*` the hands' target, `H` where the hands are, `V` their velocity.
- `b` the blade's direction (unit), `w` its angular velocity.
- `s` the steer: the part of the mouse's movement that went past the edge of the hands' field (O5), a 2D angle.

### 3.1 Input (O1, O2)

Each frame, with the mouse delta `dm = (dx, dy)` in pixels:

    H*  <-  H* + G dm,      G = diag(g_y, g_z) acting on (y, z), with g_y = 0.13 cm/px, g_z = 0.45 cm/px

`H*` lives on the surface in front of the body a comfortable forearm away, `x = x0(y, z)`. It is then pulled back into the
field the arms can reach, `F` (an ellipsoid round the shoulders, 55 cm for one arm, less across the body for two hands):

    H*_in = closest point of F to H*,     overflow e = H* - H*_in,     H* <- H*_in
    s     <- s + k_s e                     (k_s turns centimetres of overflow into degrees of steer; s eases back to 0)

The overflow is what turns the blade about the hands at the edge (O5) instead of pinning it.

### 3.2 Hands (O6, weight)

The hands follow the target as a damped spring, slower for a heavier weapon:

    V' = w_h^2 (H*_in - H) - 2 z w_h V,      w_h = w0 / sqrt(1 + m_w / m0),  z = 1

### 3.3 Blade attitude (O3, O4): the non-linear part

The point wants the enemy, `E` (a point ahead at chest height, about 1.5 m): `p = normalize(E - H)`.

The wrist cannot bend that far for every hand position: the blade must lie within a cone about the forearm's way,
`f = normalize(H - shoulder)`, the cone's half angle `a(H)` widening as the hand rises. Where `p` is outside it the blade is
the nearest way inside it, turned *outward* (away from the body's middle line), not across:

    b_t = p                                  if angle(p, f) <= a(H)
    b_t = rotate f towards p by a(H), then on round the outward side   otherwise

This is what makes it non-linear: near the middle `angle(p, f)` is small and the point stays on the enemy; past the point
where the hand is far enough out that the enemy lies beyond the wrist's reach, the blade swings out and up (upright at the
far right, back over the shoulder at the far left, hanging when the hands are up on the left, aslant across when low on the
right). The cone's half angle and its outward side are what the reference stills fix (section 4).

Then the steer (O5) turns it about the hand, and the blade follows with the weapon's weight:

    b_t <- R(s) b_t
    w'  = w_b^2 angle(b, b_t) axis - 2 z_b w_b w,     w_b smaller for a heavier or longer weapon

### 3.4 Body (O6)

    chest yaw = k_y * H_y   (about 0.9 degrees per cm of hand out to the side, the hips half of it, up to 50 degrees)

### 3.5 Shown as asked: closing the loop

What is drawn must be what the model asks, whatever way it came. The arm's physics stays (it is what makes hits and blocks
real), but the difference between where the hand and the blade really are and where the model wants them is fed back each
frame:

    c_h <- c_h + k_c (H_model - H_real) dt,   c_b <- c_b + k_c angle(b_real, b_model) dt     (each clamped)

and the arm is asked for `H_model + c_h` and `R(c_b) b_model`. This replaces the table of corrections: it is right for every
path, and a pose held still settles exactly where the model has it.

## 4. Fitting it to the stills

Every reference still gives, in the body's frame, where the hands are and which way the blade points. With the model, a
still is reproduced by a hand target `H*` (from where the hands are in the still) and the model's blade rule; the cone
`a(H)` and the outward rule are fitted so that for every still the blade the rule gives matches the still within 10 degrees.

Each still is then a test: the game is put at that `H*`, left to settle, and measured (blade direction, hand position) with
the PoseMeasure command, and shot from the still's own camera for the side-by-side sheet.

## 5. Acceptance

The weapon work is finished only when, for every reference still:

- the measured blade is within 10 degrees of the still's, the hands within 6 cm;
- the side-by-side sheet shows the same pose;
- the same pose is reached coming from two different directions (no path dependence);
- a sweep through the whole field never turns the blade more than 20 degrees in one frame (BladeWatch).

Fists by the same model: the guard is the hands' field without a weapon, slow mouse moves the guard (3.1-3.2), a quick
throw extends the arm along the aim from the guard (the punch), measured against the Half Sword punch stills the same way.
