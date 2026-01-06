#include "VertexAttribLocations.h"
#include "Vertex.h"
namespace SilverLining
{
VertexAttribLocations::VertexAttribLocations(int _vertexLocation, int _colorLocation, int _texcoordLocation)
    : vertexLocation(_vertexLocation)
    , colorLocation(_colorLocation)
    , texcoordLocation(_texcoordLocation)
{

}

VertexAttribLocations::~VertexAttribLocations()
{
    // Nothing to do
}

void VertexAttribLocations::InitFromProgram(unsigned int program)
{
    vertexLocation = glGetAttribLocation(program, "vertex");
    colorLocation = glGetAttribLocation(program, "color");
    texcoordLocation = glGetAttribLocation(program, "texcoord");
}

void VertexAttribLocations::SetUpVertexAttribPointers(void) const
{
    if (vertexLocation != -1) glVertexAttribPointer(vertexLocation, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)0);
#ifdef FLOATING_POINT_COLOR
    if (colorLocation != -1) glVertexAttribPointer(colorLocation, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(4 * sizeof(float)));
#ifdef HALF_FLOAT_TEXCOORDS
    if (texcoordLocation != -1) glVertexAttribPointer(texcoordLocation, 4, GL_HALF_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(4 * sizeof(float) + 4 * sizeof(float)));
#else
    if (texcoordLocation != -1) glVertexAttribPointer(texcoordLocation, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(4 * sizeof(float)+4 * sizeof(float)));
#endif
#else
    if (colorLocation != -1) glVertexAttribPointer(colorLocation, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertex), (GLvoid *)(4 * sizeof(float)));
#ifdef HALF_FLOAT_TEXCOORDS
    if (texcoordLocation != -1) glVertexAttribPointer(texcoordLocation, 4, GL_HALF_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(4 * sizeof(float) + 4 * sizeof(char)));
#else
    if (texcoordLocation != -1) glVertexAttribPointer(texcoordLocation, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(4 * sizeof(float) + 4 * sizeof(char)));
#endif
#endif
}

void VertexAttribLocations::EnableVertexAttribArrays(void) const
{
    if (vertexLocation != -1) glEnableVertexAttribArray(vertexLocation);
    if (colorLocation != -1) glEnableVertexAttribArray(colorLocation);
    if (texcoordLocation != -1) glEnableVertexAttribArray(texcoordLocation);
}
}