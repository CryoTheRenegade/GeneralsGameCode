/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <vector>

#include "Common/DataChunk.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/TerrainTypes.h"
#include "GameLogic/SidesList.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

namespace
{
// Keep the map fixture in memory: DataChunkOutput otherwise uses a shared temporary file.
class MapStream : public ChunkInputStream, public OutputStream
{
public:
	MapStream() : m_position(0) { }
	Int write(const void *data, Int size) override
	{
		const char *bytes = static_cast<const char *>(data);
		m_bytes.insert(m_bytes.end(), bytes, bytes + size);
		return size;
	}
	Int read(void *data, Int size) override
	{
		if (size < 0 || m_position + size > m_bytes.size()) return 0;
		memcpy(data, m_bytes.data() + m_position, size);
		m_position += size;
		return size;
	}
	UnsignedInt tell() override { return m_position; }
	Bool absoluteSeek(UnsignedInt position) override
	{
		if (position > m_bytes.size()) return false;
		m_position = position;
		return true;
	}
	Bool eof() override { return m_position == m_bytes.size(); }
	template<class T> void append(T value) { write(&value, sizeof(value)); }
	void string(const char *value)
	{
		append<UnsignedShort>(static_cast<UnsignedShort>(strlen(value)));
		write(value, static_cast<Int>(strlen(value)));
	}
	void chunk(UnsignedInt id, DataChunkVersionType version, const MapStream &data)
	{
		append(id);
		append(version);
		append<Int>(static_cast<Int>(data.m_bytes.size()));
		write(data.m_bytes.data(), static_cast<Int>(data.m_bytes.size()));
	}
private:
	std::vector<char> m_bytes;
	UnsignedInt m_position;
};

class InspectableHeightMap : public WorldHeightMap
{
public:
	explicit InspectableHeightMap(ChunkInputStream *stream) : WorldHeightMap(stream) { }
	static Bool hasAlphaTiles() { return m_alphaTiles[0] != nullptr; }
};

void makeMap(MapStream &stream)
{
	DataChunkTableOfContents contents;
	const UnsignedInt heightID = contents.allocateID("HeightMapData");
	const UnsignedInt blendID = contents.allocateID("BlendTileData");
	contents.write(stream);

	MapStream height;
	height.append<Int>(2); // Width.
	height.append<Int>(2); // Height.
	height.append<Int>(0); // Border.
	height.append<Int>(4); // Height sample count.
	for (Int i = 0; i < 4; ++i) height.append<UnsignedByte>(i + 10);
	stream.chunk(heightID, K_HEIGHT_MAP_VERSION_3, height);

	MapStream blend;
	blend.append<Int>(4);
	for (Int i = 0; i < 4; ++i) blend.append<Short>(i == 0 ? 0 : 4); // Two terrain classes.
	for (Int i = 0; i < 12; ++i) blend.append<Short>(0); // Blend, extra blend and cliff UV indices.
	blend.append<UnsignedByte>(1); // Painted cliff, independent of the gentle height slope.
	blend.append<UnsignedByte>(0);
	blend.append<Int>(2); // Bitmap tiles.
	blend.append<Int>(1); // Default blend.
	blend.append<Int>(1); // Default cliff UV.
	blend.append<Int>(2); // Texture classes.
	for (Int i = 0; i < 2; ++i)
	{
		blend.append<Int>(i); // First tile.
		blend.append<Int>(1); // Tile count.
		blend.append<Int>(1); // Width.
		blend.append<Int>(0); // Legacy GDF flag.
		blend.string(i == 0 ? "Buildable" : "Restricted");
	}
	blend.append<Int>(1); // Edge tiles.
	blend.append<Int>(1); // Edge texture classes.
	blend.append<Int>(0);
	blend.append<Int>(1);
	blend.append<Int>(1);
	blend.string("BlendEdge");
	stream.chunk(blendID, K_BLEND_TILE_VERSION_8, blend);
}

class WorldHeightMapTest : public testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		m_previousGlobal = TheWritableGlobalData;
		m_previousNames = TheNameKeyGenerator;
		m_previousSides = TheSidesList;
		m_previousTerrain = TheTerrainTypes;
		m_global.reset(new GlobalData);
		TheWritableGlobalData = m_global.get();
		TheWritableGlobalData->m_headless = GetParam();
		m_names.init();
		TheNameKeyGenerator = &m_names;
		TheSidesList = &m_sides;
		// Headless loading must not even consult the terrain asset catalog.
		TheTerrainTypes = GetParam() ? nullptr : &m_terrain;
	}
	void TearDown() override
	{
		m_sides.clear();
		m_global.reset();
		TheWritableGlobalData = m_previousGlobal;
		TheNameKeyGenerator = m_previousNames;
		TheSidesList = m_previousSides;
		TheTerrainTypes = m_previousTerrain;
	}
	std::unique_ptr<GlobalData> m_global;
	NameKeyGenerator m_names;
	SidesList m_sides;
	TerrainTypeCollection m_terrain;
	GlobalData *m_previousGlobal;
	NameKeyGenerator *m_previousNames;
	SidesList *m_previousSides;
	TerrainTypeCollection *m_previousTerrain;
};

TEST_P(WorldHeightMapTest, PreservesGameplayDataWithoutHeadlessTextureAssets)
{
	MapStream stream;
	makeMap(stream);
	std::unique_ptr<InspectableHeightMap> map(new InspectableHeightMap(&stream));
	EXPECT_EQ(map->getXExtent(), 2);
	EXPECT_EQ(map->getHeight(1, 1), 13);
	EXPECT_TRUE(map->getCliffState(0, 0));
	EXPECT_FALSE(map->getCliffState(1, 0));
	EXPECT_STREQ(map->getTerrainNameAt(0, 0).str(), "Buildable");
	EXPECT_STREQ(map->getTerrainNameAt(10, 0).str(), "Restricted");
	EXPECT_EQ(InspectableHeightMap::hasAlphaTiles(), !GetParam());
	// The minimap and terrain texture readers tolerate the missing pixels.
	RGBColor color;
	map->getTerrainColorAt(0, 0, &color);
	EXPECT_EQ(color.red, 0);
	EXPECT_EQ(color.green, 0);
	EXPECT_EQ(color.blue, 0);
	EXPECT_EQ(map->getPointerToTileData(0, 0, 1), nullptr);
}

INSTANTIATE_TEST_SUITE_P(RenderingModes, WorldHeightMapTest, testing::Bool());
}
