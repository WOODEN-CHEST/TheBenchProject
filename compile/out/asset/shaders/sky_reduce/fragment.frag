#version 330

// The sky's occupancy volume's passes over whole levels: with sourceStep 2, a level of its pyramid, each texel the union
// (OR) of the 2 x 2 texels of the level below (the blit's texture with its base level set to that level); with
// sourceStep 1, a copy of the blit's texture texel for texel (a layer of the volume ORed into it by the logic op).

in vec2 fragTexCoord;    // unused: the texel is the fragment's own
in vec4 fragColor;

uniform usampler2D texture0; // the level below (sourceStep 2) or the layer copied (sourceStep 1)
uniform int sourceStep;      // 2: reduce 2 x 2 texels into one; 1: copy one

out uvec4 finalColor;

void main()
{
    ivec2 source = ivec2(gl_FragCoord.xy)*sourceStep;
    uvec4 column = texelFetch(texture0, source, 0);
    if (sourceStep == 2)
    {
        column |= texelFetch(texture0, source + ivec2(1, 0), 0) | texelFetch(texture0, source + ivec2(0, 1), 0)
            | texelFetch(texture0, source + ivec2(1, 1), 0);
    }
    finalColor = column;
}
