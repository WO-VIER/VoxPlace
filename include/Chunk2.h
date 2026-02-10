#ifndef CHUNK2_H
#define CHUNK2_H

// Chunks Params 

constexpr unsigned char CHUNK_SIZE_X = 16; // (0 - 255)
constexpr unsigned char CHUNK_SIZE_Y = 64; // Hauteur 
constexpr unsigned char CHUNK_SIZE_Z = 16;

// 16 * 16 * 64 = 16384 octets blocs par chunk 
// 1 bloc = une couleur qui peut etre 16 ou 32 couleurs 1 (byte)

//  Chaques faces de chaque blocs sont composées de 4 points
// Liste de points : 4 * (3 float) = 12 floats par faces = 12 * 4 = 48 octets par faces
// Liste de triangles : 2 (triangles) * 3 int (indicies) = 6 int * 4 = 24 octets par faces  
//  points * positions * tailles en octet
//  triangles * indices * tailles en octet
// 72 octets par faces dans ram et vram pour chaques faces + texture par point 4 * 2 float = 104 octets par faces  


// 4  

constexpr unsigned char BEDROCK_LAYER = 0;


#endif CHUNK2_H