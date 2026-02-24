#ifndef FRUSTUM_H
#define FRUSTUM_H

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

// ============================================================================
// Frustum Culling — Méthode Gribb-Hartmann (1999)
//
// Le frustum est la pyramide tronquée de la caméra = ce qu'elle voit.
// On extrait 6 plans depuis la matrice VP (projection × view).
// Un chunk est visible ssi il est du bon côté des 6 plans.
//
// Chaque plan est un vec4 (a, b, c, d) avec :
//   (a, b, c) = normale du plan
//   d = distance à l'origine
//   ax + by + cz + d > 0 → le point est du côté visible
//
// Pour tester une AABB, on prend le point le plus éloigné
// dans la direction de la normale (positive vertex).
// Si ce point est derrière le plan → AABB entièrement invisible.
// ============================================================================

struct Frustum
{
	glm::vec4 planes[6]; // Left, Right, Bottom, Top, Near, Far

	// ════════════════════════════════════════════════════════════════
	// Extraire les 6 plans du frustum depuis la matrice VP
	//
	//   VP = Projection × View
	//	row[0] -> (1.3, 0.0, 0.0, 0.0) : Gère l'axe X (gauche/droite).
	//	row[1] -> (0.0, 1.7, 0.0, 0.0) : Gère l'axe Y (haut/bas).
	//	row[2] -> (0.0, 0.0, -1.0, -0.2) : Gère la profondeur (Near/Far).
	//	row[3] -> (0.0, 0.0, -1.0, 0.0) : Gère le W (la perspective, ce qui fait que les objets lointains rapetissent).
	//
	//
	//   Left   = row[3] + row[0]
	//   Right  = row[3] - row[0]
	//   Bottom = row[3] + row[1]
	//   Top    = row[3] - row[1]
	//   Near   = row[3] + row[2]
	//   Far    = row[3] - row[2]
	//
	// Puis on normalise chaque plan (divise par length de la normale)
	// pour que la distance signée soit en unités monde.
	// ════════════════════════════════════════════════════════════════
	void extractFromVP(const glm::mat4 &vp)
	{
		// glm stocke en column-major : vp[col][row]
		// Pour extraire row[i], on prend vp[0][i], vp[1][i], vp[2][i], vp[3][i]

		// Left   = row3 + row0
		planes[0] = glm::vec4(
			vp[0][3] + vp[0][0],
			vp[1][3] + vp[1][0],
			vp[2][3] + vp[2][0],
			vp[3][3] + vp[3][0]);

		// Right  = row3 - row0
		planes[1] = glm::vec4(
			vp[0][3] - vp[0][0],
			vp[1][3] - vp[1][0],
			vp[2][3] - vp[2][0],
			vp[3][3] - vp[3][0]); // -> Donne la normal (vecteur perpendiculaire a la surface du plan) du plan de droite qui pointe vers (gauche et vers l'arrière) l'intérieur de ce que la cam voit
								  // Le d est la distance du plan par rapport à l'origine du monde (0,0,0)
								  // Ensuite on normalise cette normale qui ramene a 1 sa longueur
		// Bottom = row3 + row1
		planes[2] = glm::vec4(
			vp[0][3] + vp[0][1],
			vp[1][3] + vp[1][1],
			vp[2][3] + vp[2][1],
			vp[3][3] + vp[3][1]);

		// Top    = row3 - row1
		planes[3] = glm::vec4(
			vp[0][3] - vp[0][1],
			vp[1][3] - vp[1][1],
			vp[2][3] - vp[2][1],
			vp[3][3] - vp[3][1]);

		// Near   = row3 + row2
		planes[4] = glm::vec4(
			vp[0][3] + vp[0][2],
			vp[1][3] + vp[1][2],
			vp[2][3] + vp[2][2],
			vp[3][3] + vp[3][2]);

		// Far    = row3 - row2
		planes[5] = glm::vec4(
			vp[0][3] - vp[0][2],
			vp[1][3] - vp[1][2],
			vp[2][3] - vp[2][2],
			vp[3][3] - vp[3][2]);

		// Normaliser chaque plan
		for (int i = 0; i < 6; i++)
		{
			float len = glm::length(glm::vec3(planes[i]));
			if (len > 0.0f)
				planes[i] /= len;
		}
	}

	// ════════════════════════════════════════════════════════════════
	// Tester si une AABB est visible dans le frustum
	//
	//   Pour chaque plan :
	//     1. Trouver le "positive vertex" (coin le plus aligné
	//        avec la normale du plan)
	//     2. Calculer distance signée = dot(normal, pVertex) + d
	//     3. Si distance < 0 → AABB est entièrement derrière ce plan
	//        → INVISIBLE
	//
	//   Si l'AABB passe les 6 tests → VISIBLE
	// ════════════════════════════════════════════════════════════════
	bool isAABBVisible(const glm::vec3 &aabbMin, const glm::vec3 &aabbMax) const
	{
		for (int i = 0; i < 6; i++)
		{
			glm::vec3 normal(planes[i]);

			// Positive vertex : le coin de l'AABB le plus loin
			// dans la direction de la normale
			glm::vec3 pVertex(
				(normal.x >= 0.0f) ? aabbMax.x : aabbMin.x,
				(normal.y >= 0.0f) ? aabbMax.y : aabbMin.y,
				(normal.z >= 0.0f) ? aabbMax.z : aabbMin.z);

			// Distance signée du positive vertex au plan
			float dist = glm::dot(normal, pVertex) + planes[i].w;

			if (dist < 0.0f)
				return false; // Entièrement derrière ce plan
		}
		return true; // Visible (du bon côté des 6 plans)
	}

	// ════════════════════════════════════════════════════════════════
	// Helper : tester un chunk directement
	//
	//
	//   AABB du chunk :
	//   min = (chunkX × 16,   0, chunkZ × 16)
	//   max = (chunkX × 16 + 16, 256, chunkZ × 16 + 16)
	// ════════════════════════════════════════════════════════════════
	bool isChunkVisible(int chunkX, int chunkZ) const
	{
		glm::vec3 aabbMin(
			static_cast<float>(chunkX * 16),
			0.0f,
			static_cast<float>(chunkZ * 16));

		glm::vec3 aabbMax(
			static_cast<float>(chunkX * 16 + 16),
			256.0f,
			static_cast<float>(chunkZ * 16 + 16));

		return isAABBVisible(aabbMin, aabbMax);
	}

	void frustumProfiler()
	{
		for (int i = 0; i < 6; i++)
		{
			switch (i)
			{
			case 0:
				std::cout << "Left plan : " << glm::to_string(planes[i]) << std::endl;
				break;
			case 1:
				std::cout << "Right plan : " << glm::to_string(planes[i]) << std::endl;
				break;
			case 2:
				std::cout << "Bottom plan : " << glm::to_string(planes[i]) << std::endl;
				break;
			case 3:
				std::cout << "Top plan : " << glm::to_string(planes[i]) << std::endl;
				break;
			case 4:
				std::cout << "Near plan : " << glm::to_string(planes[i]) << std::endl;
				break;
			case 5:
				std::cout << "Far plan : " << glm::to_string(planes[i]) << std::endl;
				break;
			};
		};
	}
};

#endif // FRUSTUM_H
