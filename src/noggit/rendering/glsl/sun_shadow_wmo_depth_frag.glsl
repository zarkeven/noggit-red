// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 410 core

// Depth-only pass: write shadow map from submitted triangles without alpha cutout.
// Alpha-tested WMO decals are rare; geometry-only casting avoids dropping domes and
// metal shells when texture alpha does not match the visible surface.

void main()
{
}
