#pragma once
#include <d3d11.h>
#include "pose-data.h"
// Successful reads transfer one COM reference per eye to the consumer.
struct SubmittedDepthImages {DepthSubmissionInfo metadata{};ID3D11Texture2D *texture[2]{};};
using ReadSubmittedDepthImages=bool(__cdecl *)(SubmittedDepthImages *,unsigned);
