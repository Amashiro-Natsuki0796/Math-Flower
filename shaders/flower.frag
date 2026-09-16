#version 330 core

in float vValue;
out vec4 FragColor;

/*
 * A compact approximation of Matplotlib's gist_rainbow.
 * The geometry and camera behavior are independent of this mapping.
 */
vec3 rainbow(float x)
{
    x = clamp(x, 0.0, 1.0);

    // Smooth HSV-like rainbow.
    float h = x * 6.0;
    float c = 1.0;
    float hp = mod(h, 6.0);

    vec3 rgb;
    if (hp < 1.0)
        rgb = vec3(c, hp, 0.0);
    else if (hp < 2.0)
        rgb = vec3(2.0 - hp, c, 0.0);
    else if (hp < 3.0)
        rgb = vec3(0.0, c, hp - 2.0);
    else if (hp < 4.0)
        rgb = vec3(0.0, 4.0 - hp, c);
    else if (hp < 5.0)
        rgb = vec3(hp - 4.0, 0.0, c);
    else
        rgb = vec3(c, 0.0, 6.0 - hp);

    return rgb;
}

void main()
{
    FragColor = vec4(rainbow(vValue), 1.0);
}
