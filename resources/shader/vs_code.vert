#version 330 core

// INPUTS: Vertex attributes from the VBO
layout(location = 0) in vec2 aPos; // Vertex position (e.g., -1.0 to 1.0)
layout(location = 1) in vec2 aTex; // Texture coordinate (0.0 to 1.0)

// OUTPUT: Data passed to the Fragment Shader
out vec2 TexCoord;

void main() {
    // Set the final clip-space position of the vertex.
    gl_Position = vec4(aPos, 0.0, 1.0);

    // Pass the texture coordinate to the fragment shader.
    TexCoord = aTex;

    // Flip the Y-axis of the texture coordinate.
    // This is often needed to align with standard image texture mapping.
    TexCoord.y = 1.0 - TexCoord.y;
}