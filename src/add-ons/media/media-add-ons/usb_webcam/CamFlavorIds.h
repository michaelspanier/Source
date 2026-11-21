/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CAM_FLAVOR_IDS_H
#define CAM_FLAVOR_IDS_H


#include <stddef.h>
#include <string.h>


/*!	Remembers the flavor ids of unplugged cameras. A camera plugged in again
	gets its old id back, so the node that streamed from it takes it over and
	the media server does not make a second node for the same camera.
	The caller locks.
*/
struct cam_flavor_entry {
			char				name[64];
			int					id;
};


class CamFlavorIds {
public:
	static	const int			kMaxRemembered = 8;

								CamFlavorIds();

			int					Take(const char* name, int freshId);
			void				Remember(const char* name, int id);

private:
			void				_RemoveAt(int index);

			cam_flavor_entry	fEntries[kMaxRemembered];
			int					fCount;
};


inline
CamFlavorIds::CamFlavorIds()
	:
	fCount(0)
{
}


/*!	Returns the remembered id of a camera with this name and forgets it,
	or \a freshId when there is none.
*/
inline int
CamFlavorIds::Take(const char* name, int freshId)
{
	if (name == NULL)
		return freshId;

	for (int i = 0; i < fCount; i++) {
		if (strcmp(fEntries[i].name, name) != 0)
			continue;
		int id = fEntries[i].id;
		_RemoveAt(i);
		return id;
	}
	return freshId;
}


/*!	Keeps the id of an unplugged camera. When the table is full, the oldest
	entry goes.
*/
inline void
CamFlavorIds::Remember(const char* name, int id)
{
	if (name == NULL)
		return;

	if (fCount == kMaxRemembered)
		_RemoveAt(0);
	strlcpy(fEntries[fCount].name, name, sizeof(fEntries[fCount].name));
	fEntries[fCount].id = id;
	fCount++;
}


inline void
CamFlavorIds::_RemoveAt(int index)
{
	memmove(&fEntries[index], &fEntries[index + 1],
		(fCount - index - 1) * sizeof(cam_flavor_entry));
	fCount--;
}


#endif	// CAM_FLAVOR_IDS_H
