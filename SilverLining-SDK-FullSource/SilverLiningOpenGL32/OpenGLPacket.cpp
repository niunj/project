#include "SLAssert.h"
#include "OpenGLPacket.h"
#include "SL_Buffer.h"
#include "SLVector.h"

namespace SilverLining
{
void GLPSEUDONAMEDBUFFERSUBDATA::execute(const GLPSEUDONAMEDBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pParams)
{
    SL_Buffer* slBuffer = pParams->slBuffer;
    SL_ASSERT(slBuffer->handle == 0);
    slBuffer->InitBuffers();
    glNamedBufferSubData(slBuffer->handle, pParams->offset, pParams->size, pParams->data);
}

void GLPSEUDOSYNC::execute(const GLPSEUDOSYNC* SILVERLINING_RESTRICT_KEYWORD pParams)
{
    SL_Buffer* slBuffer = pParams->slBuffer;
    SL_ASSERT(slBuffer);
    if (slBuffer == 0) {
        std::cout << "slBuffer == 0" << std::endl;
        return;
    }
    if (slBuffer->handle == 0) {
        slBuffer->InitBuffers();
    }
    if (slBuffer->handle == 0) {
        std::cout << "slBuffer->handle == 0" << std::endl;
        return;
    }

    const int numVerts = (int)(pParams->size / sizeof(Vertex));

    if (pParams->justColors == false) {
        slBuffer->SyncToGPU(pParams->offset, pParams->size, (unsigned char*)((unsigned char*)pParams->scratchVerticesWithColors->data() + pParams->startIndexColors*sizeof(Vertex)));
    } else {
        SL_ASSERT(pParams->scratchVerticesToGetFromVertexBufferAndCopyColorsInto != 0);
        slBuffer->GetData(pParams->offset, pParams->size, &(pParams->scratchVerticesToGetFromVertexBufferAndCopyColorsInto->at(pParams->startIndexCopyInto)));
        for (int i = 0; i < (int)numVerts; ++i) {

            Vertex* srcVertex = (Vertex*)(&(pParams->scratchVerticesWithColors->at(pParams->startIndexColors + i)));
            Vertex* dstVertex = (Vertex*)(&(pParams->scratchVerticesToGetFromVertexBufferAndCopyColorsInto->at(pParams->startIndexCopyInto + i)));
            if (srcVertex->r<0 && srcVertex->g<0 && srcVertex->b<0  && srcVertex->a<0) {
                continue;
            }
            dstVertex->r = srcVertex->r;
            dstVertex->g = srcVertex->g;
            dstVertex->b = srcVertex->b;
            dstVertex->a = srcVertex->a;
        }
        slBuffer->SyncToGPU(pParams->offset, pParams->size, (unsigned char*)((unsigned char*)pParams->scratchVerticesToGetFromVertexBufferAndCopyColorsInto->data() + pParams->startIndexCopyInto*sizeof(Vertex)));
    }
}
}

