#include <noggit/World.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <time.h>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>

#include <noggit/DBC.h>
#include <noggit/Environment.h>
#include <noggit/Log.h>
#include <noggit/MapChunk.h>
#include <noggit/MapTile.h>
#include <noggit/Misc.h>
#include <noggit/ModelManager.h> // ModelManager
#include <noggit/Project.h>
#include <noggit/Settings.h>
#include <noggit/TextureManager.h>
#include <noggit/UITexturingGUI.h>
#include <noggit/Video.h>
#include <noggit/WMOInstance.h> // WMOInstance
#include <noggit/MapTile.h>
#include <noggit/Brush.h> // brush
#include <noggit/ConfigFile.h>

GLuint selectionBuffer[8192];

void renderSphere(float x1, float y1, float z1, float x2, float y2, float z2, float radius, int subdivisions, GLUquadricObj *quadric)
{
  float vx = x2-x1;
  float vy = y2-y1;
  float vz = z2-z1;

  //handle the degenerate case of z1 == z2 with an approximation
  if( vz == 0.0f )
    vz = .0001f;

  float v = sqrt( vx*vx + vy*vy + vz*vz );
  float ax = 57.2957795f*acos( vz/v );
  if ( vz < 0.0f )
    ax = -ax;
  float rx = -vy*vz;
  float ry = vx*vz;
  glPushMatrix();

  //draw the quadric
  glTranslatef( x1,y1,z1 );
  glRotatef(ax, rx, ry, 0.0);

  gluQuadricOrientation(quadric,GLU_OUTSIDE);
  gluSphere(quadric, radius, subdivisions , subdivisions );

  glPopMatrix();
}

void renderSphere_convenient(float x, float y, float z, float radius, int subdivisions)
{
  if(Environment::getInstance()->screenX>0 && Environment::getInstance()->screenY>0)
  {
    //the same quadric can be re-used for drawing many objects
    glDisable(GL_LIGHTING);
    glColor4f(Environment::getInstance()->cursorColorR, Environment::getInstance()->cursorColorG, Environment::getInstance()->cursorColorB, Environment::getInstance()->cursorColorA );
    GLUquadricObj *quadric=gluNewQuadric();
    gluQuadricNormals(quadric, GLU_SMOOTH);
    renderSphere(x,y,z,x,y,z,0.3f,15,quadric);
    renderSphere(x,y,z,x,y,z,radius,subdivisions,quadric);
    gluDeleteQuadric(quadric);
    glEnable(GL_LIGHTING);
  }
}

void renderDisk(float x1, float y1, float z1, float x2, float y2, float z2, float radius, int subdivisions, GLUquadricObj *quadric)
{
  float vx = x2 - x1;
  float vy = y2 - y1;
  float vz = z2 - z1;

  //handle the degenerate case of z1 == z2 with an approximation
  if( vz == 0.0f )
    vz = .0001f;

  float v = sqrt( vx*vx + vy*vy + vz*vz );
  float ax = 57.2957795f*acos( vz/v );
  if(vz < 0.0f)
    ax = -ax;

  float rx = -vy * vz;
  float ry = vx * vz;

  glPushMatrix();
  glDisable(GL_DEPTH_TEST);
  glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
  glEnable(GL_COLOR_MATERIAL);

  //draw the quadric
  glTranslatef(x1, y1, z1);
  glRotatef(ax, rx, ry, 0.0f);
  glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
  glColor4f(Environment::getInstance()->cursorColorR, Environment::getInstance()->cursorColorG, Environment::getInstance()->cursorColorB, Environment::getInstance()->cursorColorA);

  gluQuadricOrientation(quadric, GLU_OUTSIDE);
  gluDisk(quadric, radius - 0.25f, radius + 5.0f, subdivisions, 2);

  //glColor4f(0.0f, 0.8f, 0.1f, 0.9f);
  //gluDisk(quadric, (radius * 1.5) - 2, (radius * 1.5) + 2, 0, 1);
  glEnable(GL_DEPTH_TEST);
  glPopMatrix();
}

void renderDisk_convenient(float x, float y, float z, float radius)
{
  int subdivisions =(int)radius * 1.5;
  if( subdivisions < 15 ) subdivisions=15;
  glDisable(GL_LIGHTING);
  GLUquadricObj *quadric = gluNewQuadric();
  gluQuadricDrawStyle(quadric, GLU_LINE);
  gluQuadricNormals(quadric, GLU_SMOOTH);
  renderDisk(x, y, z, x, y, z, radius, subdivisions, quadric);
  renderSphere(x,y,z,x,y,z,0.3,15,quadric);
  gluDeleteQuadric(quadric);
  glEnable(GL_LIGHTING);
}

bool World::IsEditableWorld( int pMapId )
{
  std::string lMapName;
  try
  {
    DBCFile::Record map = gMapDB.getByID( pMapId );
    lMapName = map.getString( MapDB::InternalName );
  }
  catch( ... )
  {
    LogError << "Did not find map with id " << pMapId << ". This is NOT editable.." << std::endl;
    return false;
  }

  std::stringstream ssfilename;
  ssfilename << "World\\Maps\\" << lMapName << "\\" << lMapName << ".wdt";

  if( !MPQFile::exists( ssfilename.str() ) )
  {
    Log << "World " << pMapId << ": " << lMapName << " has no WDT file!" << std::endl;
    return false;
  }

  MPQFile mf( ssfilename.str() );

  //sometimes, wdts don't open, so ignore them...
  if(mf.isEof())
    return false;

  const char * lPointer = reinterpret_cast<const char*>( mf.getPointer() );

  // Not using the libWDT here doubles performance. You might want to look at your lib again and improve it.
  const int lFlags = *( reinterpret_cast<const int*>( lPointer + 8 + 4 + 8 ) );
  if( lFlags & 1 )
    return false;

  const int * lData = reinterpret_cast<const int*>( lPointer + 8 + 4 + 8 + 0x20 + 8 );
  for( int i = 0; i < 8192; i += 2 )
  {
    if( lData[i] & 1 )
      return true;
  }

  return false;
}

World::World( const std::string& name )
  : cx( -1 )
  , cz( -1 )
  , ex( -1 )
  , ez( -1 )
  , mCurrentSelection( NULL )
  , mCurrentSelectedTriangle( 0 )
  , SelectionMode( false )
  , mBigAlpha( false )
  , mWmoFilename( "" )
  , mWmoEntry( ENTRY_MODF() )
  , detailtexcoords( 0 )
  , alphatexcoords( 0 )
  , mMapId( 0xFFFFFFFF )
  , ol( NULL )
  , l_const( 0.0f )
  , l_linear( 0.7f )
  , l_quadratic( 0.03f )
  , drawlines( false )
  , drawmodels( true )
  , drawterrain( true )
  , drawwater( false )
  , drawwmo( true )
  , lighting( true )
  , animtime( 0 )
  , time( 1450 )
  , basename( name )
  , fogdistance( 777.0f )
  , culldistance( fogdistance )
  , autoheight( false )
  , minX( 0.0f )
  , maxX( 0.0f )
  , minY( 0.0f )
  , maxY( 0.0f )
  , zoom( 0.25f )
  , skies( NULL )
  , mHasAGlobalWMO( false )
  , loading( false )
  , noadt( false )
  , outdoorLightStats( OutdoorLightStats() )
  , camera( Vec3D( 0.0f, 0.0f, 0.0f ) )
  , lookat( Vec3D( 0.0f, 0.0f, 0.0f ) )
  , frustum( Frustum() )
  , _selection_names (this)
{
  for( DBCFile::Iterator i = gMapDB.begin(); i != gMapDB.end(); ++i )
  {
    if( name == std::string( i->getString( MapDB::InternalName ) ) )
    {
      mMapId = i->getUInt( MapDB::MapID );
      break;
    }
  }
  if( mMapId == 0xFFFFFFFF )
    LogError << "MapId for \"" << name << "\" not found! What is wrong here?" << std::endl;

  LogDebug << "Loading world \"" << name << "\"." << std::endl;

  for( size_t j = 0; j < 64; ++j )
  {
    for( size_t i = 0; i < 64; ++i )
    {
      lowrestiles[j][i] = NULL;
    }
  }

  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << ".wdt";

  MPQFile theFile(filename.str());
  uint32_t fourcc;
  //uint32_t size;

  // - MVER ----------------------------------------------

  uint32_t version;

  theFile.read( &fourcc, 4 );
  theFile.seekRelative( 4 );
  theFile.read( &version, 4 );

  //! \todo find the correct version of WDT files.
  assert( fourcc == 'MVER' && version == 18 );

  // - MHDR ----------------------------------------------

  uint32_t flags;

  theFile.read( &fourcc, 4 );
  theFile.seekRelative( 4 );

  assert( fourcc == 'MPHD' );

  theFile.read( &flags, 4 );
  theFile.seekRelative( 4 * 7 );

  mHasAGlobalWMO = flags & 1;
  mBigAlpha = flags & 4;

  // - MAIN ----------------------------------------------

  theFile.read( &fourcc, 4 );
  theFile.seekRelative( 4 );

  assert( fourcc == 'MAIN' );

  /// this is the theory. Sadly, we are also compiling on 64 bit machines with size_t being 8 byte, not 4. Therefore, we can't do the same thing, Blizzard does in its 32bit executable.
  //theFile.read( &(mTiles[0][0]), sizeof( 8 * 64 * 64 ) );

  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      theFile.read( &mTiles[j][i].flags, 4 );
      theFile.seekRelative( 4 );
      mTiles[j][i].tile = NULL;
    }
  }

  if( !theFile.isEof() )
  {
    //! \note We actually don't load WMO only worlds, so we just stop reading here, k?
    //! \bug MODF reads wrong. The assertion fails every time. Somehow, it keeps being MWMO. Or are there two blocks?

    mHasAGlobalWMO = false;

#ifdef __ASSERTIONBUGFIXED

    // - MWMO ----------------------------------------------

    theFile.read( &fourcc, 4 );
    theFile.read( &size, 4 );

    assert( fourcc == 'MWMO' );

    char * wmoFilenameBuf = new char[size];
    theFile.read( &wmoFilenameBuf, size );

    mWmoFilename = wmoFilenameBuf;

    free(wmoFilenameBuf);

    // - MODF ----------------------------------------------

    theFile.read( &fourcc, 4 );
    theFile.seekRelative( 4 );

    assert( fourcc == 'MODF' );

    theFile.read( &mWmoEntry, sizeof( ENTRY_MODF ) );

#endif //__ASSERTIONBUGFIXED

  }

  // -----------------------------------------------------

  theFile.close();

  if( !mHasAGlobalWMO )
    initMinimap();

  // don't load map objects while still on the menu screen
  //initDisplay();
}

static inline QRgb color_for_height (int16_t height)
{
  struct ranged_color
  {
    const QRgb color;
    const int16_t start;
    const int16_t stop;

    ranged_color (const QRgb& color, const int16_t& start, const int16_t& stop)
    : color (color), start (start), stop (stop) {}
  };

  static const ranged_color colors[] =
    { ranged_color (qRgb (20, 149, 7), 0, 600)
    , ranged_color (qRgb (137, 84, 21), 600, 1200)
    , ranged_color (qRgb (96, 96, 96), 1200, 1600)
    , ranged_color (qRgb (255, 255, 255), 1600, 0x7FFF)
    };
  static const size_t num_colors (sizeof (colors) / sizeof (ranged_color));

  if (height < colors[0].start)
  {
    return qRgb (0, 0, 255 + qMax (height / 2.0, -255.0));
  }
  else if (height >= colors[num_colors - 1].stop)
  {
    return colors[num_colors].color;
  }

  qreal t (1.0);
  size_t correct_color (num_colors - 1);

  for (size_t i (0); i < num_colors - 1; ++i)
  {
    if (height >= colors[i].start && height < colors[i].stop)
    {
      t = (height - colors[i].start) / qreal (colors[i].stop - colors[i].start);
      correct_color = i;
      break;
    }
  }

  return qRgb ( qRed (colors[correct_color + 1].color) * t + qRed (colors[correct_color].color) * (1.0 - t)
              , qGreen (colors[correct_color + 1].color) * t + qGreen (colors[correct_color].color) * (1.0 - t)
              , qBlue (colors[correct_color + 1].color) * t + qBlue (colors[correct_color].color) * (1.0 - t)
              );
}


void World::initMinimap()
{
  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << ".wdl";

  MPQFile wdl_file (filename.str());
  if (wdl_file.isEof())
  {
    LogError << "file \"World\\Maps\\" << basename << "\\" << basename << ".wdl\" does not exist." << std::endl;
    return;
  }

  uint32_t fourcc;
  uint32_t size;

  // - MVER ----------------------------------------------

  uint32_t version;

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);
  wdl_file.read (&version, 4);

  assert (fourcc == 'MVER' && size == 4 && version == 18);

  // - MWMO ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MWMO');
      // Filenames for WMO that appear in the low resolution map. Zero terminated strings.

  wdl_file.seekRelative (size);

  // - MWID ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MWID');
      // List of indexes into the MWMO chunk.

  wdl_file.seekRelative (size);

  // - MODF ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MODF');
      // Placement information for the WMO. Appears to be the same 64 byte structure used in the WDT and ADT MODF chunks.

  wdl_file.seekRelative (size);

  // - MAOF ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MAOF' && size == 64 * 64 * sizeof (uint32_t));

  uint32_t mare_offsets[64][64];
  wdl_file.read (mare_offsets, 64 * 64 * sizeof (uint32_t));

  // - MARE and MAHO by offset ---------------------------

  _minimap = QImage (17 * 64, 17 * 64, QImage::Format_RGB32);
  _minimap.fill (Qt::transparent);

  for (size_t y (0); y < 64; ++y)
  {
    for (size_t x (0); x < 64; ++x)
    {
      if (mare_offsets[y][x])
      {
        const uint32_t* magic (wdl_file.get<uint32_t> (mare_offsets[y][x]));
        const uint32_t* size (wdl_file.get<uint32_t> (mare_offsets[y][x] + 4));

        assert (*magic == 'MARE' && *size == 0x442);

        //! \todo There also is a second heightmap appended which has additional 16*16 pixels.
        //! \todo There also is MAHO giving holes into this heightmap.

        const int16_t* data (wdl_file.get<int16_t> (mare_offsets[y][x] + 8));

        for (size_t j (0); j < 17; ++j)
        {
          for (size_t i (0); i < 17; ++i)
          {
            _minimap.setPixel (x * 17 + i, y * 17 + j, color_for_height (data[j * 17 + i]));
              }
            }
          }
        }
      }
}

void World::initLowresTerrain()
{
  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << ".wdl";

  MPQFile wdl_file (filename.str());
  if (wdl_file.isEof())
  {
    LogError << "file \"World\\Maps\\" << basename << "\\" << basename << ".wdl\" does not exist." << std::endl;
      return;
    }

  uint32_t fourcc;
  uint32_t size;

  // - MVER ----------------------------------------------

  uint32_t version;

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);
  wdl_file.read (&version, 4);

  assert (fourcc == 'MVER' && size == 4 && version == 18);

  // - MWMO ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MWMO');
  // Filenames for WMO that appear in the low resolution map. Zero terminated strings.

  wdl_file.seekRelative (size);

  // - MWID ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MWID');
  // List of indexes into the MWMO chunk.

  wdl_file.seekRelative (size);

  // - MODF ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MODF');
  // Placement information for the WMO. Appears to be the same 64 byte structure used in the WDT and ADT MODF chunks.

  wdl_file.seekRelative (size);

  // - MAOF ----------------------------------------------

  wdl_file.read (&fourcc, 4);
  wdl_file.read (&size, 4);

  assert (fourcc == 'MAOF' && size == 64 * 64 * sizeof (uint32_t));

  uint32_t mare_offsets[64][64];
  wdl_file.read (mare_offsets, 64 * 64 * sizeof (uint32_t));

  // - MARE and MAHO by offset ---------------------------

  for (size_t y (0); y < 64; ++y)
      {
    for (size_t x (0); x < 64; ++x)
        {
      if (mare_offsets[y][x])
          {
        const uint32_t* magic (wdl_file.get<uint32_t> (mare_offsets[y][x]));
        const uint32_t* size (wdl_file.get<uint32_t> (mare_offsets[y][x] + 4));

        assert (*magic == 'MARE' && *size == 0x442);

        Vec3D vertices_17[17][17];
        Vec3D vertices_16[16][16];

        const int16_t* data_17 (wdl_file.get<int16_t> (mare_offsets[y][x] + 8));

        for (size_t j (0); j < 17; ++j)
            {
          for (size_t i (0); i < 17; ++i)
              {
            vertices_17[j][i] = Vec3D ( TILESIZE * (x + i / 16.0f)
                                      , data_17[j * 17 + i]
                                      , TILESIZE * (y + j / 16.0f)
                                      );
              }
            }

        const int16_t* data_16 (wdl_file.get<int16_t> (mare_offsets[y][x] + 8 + 17 * 17 * sizeof (int16_t)));

        for (size_t j (0); j < 16; ++j)
            {
          for (size_t i (0); i < 16; ++i)
              {
            vertices_16[j][i] = Vec3D ( TILESIZE * (x + (i + 0.5f) / 16.0f)
                                      , data_16[j * 16 + i]
                                      , TILESIZE * (y + (j + 0.5f) / 16.0f)
                                      );
              }
            }

        lowrestiles[y][x] = new OpenGL::CallList();
        lowrestiles[y][x]->startRecording();

        //! \todo Make a strip out of this.
            glBegin( GL_TRIANGLES );
        for (size_t j (0); j < 16; ++j )
            {
          for (size_t i (0); i < 16; ++i )
              {
            glVertex3fv (vertices_17[j][i]);
            glVertex3fv (vertices_16[j][i]);
            glVertex3fv (vertices_17[j][i + 1]);
            glVertex3fv (vertices_17[j][i + 1]);
            glVertex3fv (vertices_16[j][i]);
            glVertex3fv (vertices_17[j + 1][i + 1]);
            glVertex3fv (vertices_17[j + 1][i + 1]);
            glVertex3fv (vertices_16[j][i]);
            glVertex3fv (vertices_17[j + 1][i]);
            glVertex3fv (vertices_17[j + 1][i]);
            glVertex3fv (vertices_16[j][i]);
            glVertex3fv (vertices_17[j][i]);
              }
            }
            glEnd();

        lowrestiles[y][x]->endRecording();

        //! \todo There also is MAHO giving holes into this heightmap.
             }
             }
          }
        }

void initGlobalVBOs( GLuint* pDetailTexCoords, GLuint* pAlphaTexCoords )
{
  if( !*pDetailTexCoords && !*pAlphaTexCoords )
  {
    Vec2D temp[mapbufsize], *vt;
    float tx,ty;

    // init texture coordinates for detail map:
    vt = temp;
    const float detail_half = 0.5f * detail_size / 8.0f;
    for (int j=0; j<17; ++j) {
      for (int i=0; i<((j%2)?8:9); ++i) {
        tx = detail_size / 8.0f * i;
        ty = detail_size / 8.0f * j * 0.5f;
        if (j%2) {
          // offset by half
          tx += detail_half;
        }
        *vt++ = Vec2D(tx, ty);
      }
    }

    glGenBuffers(1, pDetailTexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, *pDetailTexCoords);
    glBufferData(GL_ARRAY_BUFFER, sizeof(temp), temp, GL_STATIC_DRAW);

    // init texture coordinates for alpha map:
    vt = temp;

    const float alpha_half = 0.5f * (62.0f/64.0f) / 8.0f;
    for (int j=0; j<17; ++j) {
      for (int i=0; i<((j%2)?8:9); ++i) {
        tx = (62.0f/64.0f) / 8.0f * i;
        ty = (62.0f/64.0f) / 8.0f * j * 0.5f;
        if (j%2) {
          // offset by half
          tx += alpha_half;
        }
        *vt++ = Vec2D(tx, ty);
      }
    }

    glGenBuffers(1, pAlphaTexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, *pAlphaTexCoords);
    glBufferData(GL_ARRAY_BUFFER, sizeof(temp), temp, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
}


void World::initDisplay()
{
  initGlobalVBOs( &detailtexcoords, &alphatexcoords );

  noadt = false;

  if( mHasAGlobalWMO )
  {
    WMOInstance inst( this, WMOManager::add( this, mWmoFilename ), &mWmoEntry );

    mWMOInstances.insert( std::pair<int,WMOInstance>( mWmoEntry.uniqueID, inst ) );
    camera = inst.pos;
  }

  skies = new Skies( mMapId );

  ol = new OutdoorLighting("World\\dnc.db");

  initLowresTerrain();
}

World::~World()
{

  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( lowrestiles[j][i] )
      {
        delete lowrestiles[j][i];
        lowrestiles[j][i] = NULL;
      }
      if( tileLoaded( j, i ) )
      {
        delete mTiles[j][i].tile;
        mTiles[j][i].tile = NULL;
      }
    }
  }

  if (skies)
  {
    delete skies;
    skies = NULL;
  }
  if (ol)
  {
    delete ol;
    ol = NULL;
  }

  LogDebug << "Unloaded world \"" << basename << "\"." << std::endl;
}

inline bool oktile( int z, int x )
{
  return !( z < 0 || x < 0 || z > 64 || x > 64 );
}

bool World::hasTile( int pZ, int pX ) const
{
  return oktile( pZ, pX ) && ( mTiles[pZ][pX].flags & 1 );
}

void World::enterTile( int x, int z )
{
  if( !hasTile( z, x ) )
  {
    noadt = true;
    return;
  }

  noadt = false;

  cx = x;
  cz = z;
  for( int i = std::max(cz - 2, 0); i < std::min(cz + 2, 64); ++i )
  {
    for( int j = std::max(cx - 2, 0); j < std::min(cx + 2, 64); ++j )
    {
      mTiles[i][j].tile = loadTile( i, j );
    }
  }

  if( autoheight && tileLoaded( cz, cx ) ) //ZX STEFF HERE SWAP!
  {
    float maxHeight = mTiles[cz][cx].tile->getMaxHeight();
    maxHeight = std::max( maxHeight, 0.0f );
    camera.y = maxHeight + 50.0f;

    autoheight = false;
  }
}

void World::reloadTile(int x, int z)
{
  if( tileLoaded( z, x ) )
  {
    delete mTiles[z][x].tile;
    mTiles[z][x].tile = NULL;

    std::stringstream filename;
    filename << "World\\Maps\\" << basename << "\\" << basename << "_" << x << "_" << z << ".adt";

    mTiles[z][x].tile = new MapTile( this, x, z, filename.str(), mBigAlpha );
    enterTile( cx, cz );
  }
}

void World::saveTile(int x, int z)
{
  // save goven tile
  if( tileLoaded( z, x ) )
  {
    mTiles[z][x].tile->saveTile();
  }
}

void World::saveChanged()
{
  // save all changed tiles
  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        if(getChanged(j,i))
        {
          mTiles[j][i].tile->saveTile();
          unsetChanged(j,i);
        }
      }
    }
  }

}

inline bool World::tileLoaded(int z, int x)
{
  return hasTile( z, x ) && mTiles[z][x].tile;
}

MapTile* World::loadTile(int z, int x)
{
  if( !hasTile( z, x ) )
  {
    return NULL;
  }

  if( tileLoaded( z, x ) )
  {
    return mTiles[z][x].tile;
  }

  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << "_" << x << "_" << z << ".adt";

  if( !MPQFile::exists( filename.str() ) )
  {
    LogError << "The requested tile \"" << filename.str() << "\" does not exist! Oo" << std::endl;
    return NULL;
  }

  mTiles[z][x].tile = new MapTile( this, x, z, filename.str(), mBigAlpha );// XZ STEFF Swap MapTile( z, x, file
  return mTiles[z][x].tile;
}



void lightingDefaults()
{
  glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1);
  glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0);
  glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0);

  glEnable(GL_LIGHT0);
  // wtf
  glDisable(GL_LIGHT1);
  glDisable(GL_LIGHT2);
  glDisable(GL_LIGHT3);
  glDisable(GL_LIGHT4);
  glDisable(GL_LIGHT5);
  glDisable(GL_LIGHT6);
  glDisable(GL_LIGHT7);
}

/*
void myFakeLighting()
{
  GLfloat la = 0.5f;
  GLfloat ld = 1.0f;

  GLfloat LightAmbient[] = {la, la, la, 1.0f};
  GLfloat LightDiffuse[] = {ld, ld, ld, 1.0f};
  GLfloat LightPosition[] = {-10.0f, 20.0f, -10.0f, 0.0f};
  glLightfv(GL_LIGHT0, GL_AMBIENT, LightAmbient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, LightDiffuse);
  glLightfv(GL_LIGHT0, GL_POSITION,LightPosition);
}
*/

void World::outdoorLighting()
{
  Vec4D black(0,0,0,0);
  Vec4D ambient(skies->colorSet[LIGHT_GLOBAL_AMBIENT], 1);
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

  float di = outdoorLightStats.dayIntensity;
  //float ni = outdoorLightStats.nightIntensity;

  Vec3D dd = outdoorLightStats.dayDir;
  // HACK: let's just keep the light source in place for now
  //Vec4D pos(-1, 1, -1, 0);
  Vec4D pos(-dd.x, -dd.z, dd.y, 0.0f);
  Vec4D col(skies->colorSet[LIGHT_GLOBAL_DIFFUSE] * di, 1.0f);
  glLightfv(GL_LIGHT0, GL_AMBIENT, black);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, col);
  glLightfv(GL_LIGHT0, GL_POSITION, pos);

  /*
  dd = outdoorLightStats.nightDir;
  pos(-dd.x, -dd.z, dd.y, 0.0f);
  col(skies->colorSet[LIGHT_GLOBAL_DIFFUSE] * ni, 1.0f);
  glLightfv(GL_LIGHT1, GL_AMBIENT, black);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, col);
  glLightfv(GL_LIGHT1, GL_POSITION, pos);*/
}

/*void World::outdoorLighting2()
{
  Vec4D black(0,0,0,0);
  Vec4D ambient(skies->colorSet[LIGHT_GLOBAL_AMBIENT], 1);
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

  float di = outdoorLightStats.dayIntensity, ni = outdoorLightStats.nightIntensity;
  di = 1;
  ni = 0;

  //Vec3D dd = outdoorLightStats.dayDir;
  // HACK: let's just keep the light source in place for now
  Vec4D pos(-1, -1, -1, 0);
  Vec4D col(skies->colorSet[LIGHT_GLOBAL_DIFFUSE] * di, 1);
  glLightfv(GL_LIGHT0, GL_AMBIENT, black);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, col);
  glLightfv(GL_LIGHT0, GL_POSITION, pos);
  */
  /*
  Vec3D dd = outdoorLightStats.nightDir;
  Vec4D pos(-dd.x, -dd.z, dd.y, 0);
  Vec4D col(skies->colorSet[LIGHT_GLOBAL_DIFFUSE] * ni, 1);
  glLightfv(GL_LIGHT1, GL_AMBIENT, black);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, col);
  glLightfv(GL_LIGHT1, GL_POSITION, pos);
  */ /*
}*/


void World::outdoorLights(bool on)
{
  float di = outdoorLightStats.dayIntensity;
  float ni = outdoorLightStats.nightIntensity;

  if (on) {
    Vec4D ambient(skies->colorSet[LIGHT_GLOBAL_AMBIENT], 1);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    if (di>0) {
      glEnable(GL_LIGHT0);
    } else {
      glDisable(GL_LIGHT0);
    }
    if (ni>0) {
      glEnable(GL_LIGHT1);
    } else {
      glDisable(GL_LIGHT1);
    }
  } else {
    Vec4D ambient(0, 0, 0, 1);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHT1);
  }
}

void World::setupFog (bool draw_fog)
{
  if (draw_fog)
  {

    //float fogdist = 357.0f; // minimum draw distance in wow
    //float fogdist = 777.0f; // maximum draw distance in wow

    float fogdist = fogdistance;
    float fogstart = 0.5f;

    culldistance = fogdist;

    //FOG_COLOR
    Vec4D fogcolor(skies->colorSet[FOG_COLOR], 1);
    glFogfv(GL_FOG_COLOR, fogcolor);
    //! \todo  retreive fogstart and fogend from lights.lit somehow
    glFogf(GL_FOG_END, fogdist);
    glFogf(GL_FOG_START, fogdist * fogstart);

    glEnable(GL_FOG);
  }
  else
  {
    glDisable(GL_FOG);
    culldistance = mapdrawdistance;
  }
}

void World::draw ( bool draw_terrain_height_contour
                 , bool mark_impassable_chunks
                 , bool draw_area_id_overlay
                 , bool dont_draw_cursor
                 , float inner_cursor_radius
                 , float outer_cursor_radius
                 , bool draw_wmo_doodads
                 , bool draw_fog
                 )
{
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  gluLookAt(camera.x,camera.y,camera.z, lookat.x,lookat.y,lookat.z, 0, 1, 0);

  frustum.retrieve();

  ///glDisable(GL_LIGHTING);
  ///glColor4f(1,1,1,1);

  bool hadSky (false);
  if( drawwmo || mHasAGlobalWMO )
    for( std::map<int, WMOInstance>::iterator it = mWMOInstances.begin(); !hadSky && it != mWMOInstances.end(); ++it )
      hadSky = hadSky || it->second.wmo->drawSkybox (this, camera, it->second.extents[0], it->second.extents[1]);

  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_FOG);

  int daytime = static_cast<int>(time) % 2880;
  //outdoorLightStats = ol->getLightStats(daytime);
  skies->initSky(camera, daytime);

  if (!hadSky)
    hadSky = skies->drawSky(this, camera);

  // clearing the depth buffer only - color buffer is/has been overwritten anyway
  // unless there is no sky OR skybox
  GLbitfield clearmask = GL_DEPTH_BUFFER_BIT;
  if (!hadSky)   clearmask |= GL_COLOR_BUFFER_BIT;
  glClear(clearmask);

  glDisable(GL_TEXTURE_2D);

  outdoorLighting();
  outdoorLights(true);

  glFogi(GL_FOG_MODE, GL_LINEAR);
  setupFog (draw_fog);

  // Draw verylowres heightmap
  if (draw_fog && drawterrain) {
    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glColor3fv(skies->colorSet[FOG_COLOR]);
    //glColor3f(0,1,0);
    //glDisable(GL_FOG);
    const int lrr = 2;
    for (int i=cx-lrr; i<=cx+lrr; ++i) {
      for (int j=cz-lrr; j<=cz+lrr; ++j) {
        //! \todo  some annoying visual artifacts when the verylowres terrain overlaps
        // maptiles that are close (1-off) - figure out how to fix.
        // still less annoying than hoels in the horizon when only 2-off verylowres tiles are drawn
        if ( !(i==cx&&j==cz) && oktile(i,j) && lowrestiles[j][i])
        {
          lowrestiles[j][i]->render();
        }
      }
    }
    //glEnable(GL_FOG);
  }

  // Draw height map
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL); // less z-fighting artifacts this way, I think
  glEnable(GL_LIGHTING);

  glEnable(GL_COLOR_MATERIAL);
  //glColorMaterial(GL_FRONT, GL_DIFFUSE);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  glColor4f(1,1,1,1);
  // if we're using shaders let's give it some specular
  if (video.mSupportShaders) {
    Vec4D spec_color(0.1,0.1,0.1,0.1);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec_color);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 5);

    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
  }

  glEnable(GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glClientActiveTexture(GL_TEXTURE0);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, detailtexcoords);
  glTexCoordPointer(2, GL_FLOAT, 0, 0);

  glClientActiveTexture(GL_TEXTURE1);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, alphatexcoords);
  glTexCoordPointer(2, GL_FLOAT, 0, 0);

  glClientActiveTexture(GL_TEXTURE0);

  OpenGL::SettingsSaver::save();

  // height map w/ a zillion texture passes
  //! \todo  Do we need to push the matrix here?

  glPushMatrix();

  if( drawterrain )
  {
    for( int j = 0; j < 64; ++j )
    {
      for( int i = 0; i < 64; ++i )
      {
        if( tileLoaded( j, i ) )
        {
          mTiles[j][i].tile->draw ( draw_terrain_height_contour
                                  , mark_impassable_chunks
                                  , draw_area_id_overlay
                                  , dont_draw_cursor
                                  );
        }
      }
    }
  }

  glPopMatrix();

  // Selection circle
  if( IsSelection( eEntry_MapChunk )  )
  {
    //int poly = GetCurrentSelectedTriangle();

    glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

    //nameEntry * Selection = GetCurrentSelection();

    //if( !Selection->data.mapchunk->strip )
    // Selection->data.mapchunk->initStrip();


    GLint viewport[4];
    GLdouble modelview[16];
    GLdouble projection[16];
    GLfloat winX, winY, winZ;
    GLdouble posX, posY, posZ;

    glGetDoublev( GL_MODELVIEW_MATRIX, modelview );
    glGetDoublev( GL_PROJECTION_MATRIX, projection );
    glGetIntegerv( GL_VIEWPORT, viewport );


    winX = (float)Environment::getInstance()->screenX;
    winY = (float)viewport[3] - (float)Environment::getInstance()->screenY;

    glReadPixels( Environment::getInstance()->screenX, int(winY), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &winZ );
    gluUnProject( winX, winY, winZ, modelview, projection, viewport, &posX, &posY, &posZ);

    Environment::getInstance()->Pos3DX = posX;
    Environment::getInstance()->Pos3DY = posY;
    Environment::getInstance()->Pos3DZ = posZ;

    glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
    glDisable(GL_CULL_FACE);
    //glDepthMask(false);
    //glDisable(GL_DEPTH_TEST);

    if (!dont_draw_cursor)
    {
      if(Environment::getInstance()->cursorType == 1)
        renderDisk_convenient(posX, posY, posZ, outer_cursor_radius);
      else if(Environment::getInstance()->cursorType == 2)
        renderSphere_convenient(posX, posY, posZ, outer_cursor_radius, 15);
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    //GlDepthMask(true);
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

  }


  if (drawlines)
  {
    glDisable(GL_COLOR_MATERIAL);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    setupFog (draw_fog);
    for( int j = 0; j < 64; ++j )
    {
      for( int i = 0; i < 64; ++i )
      {
        if( tileLoaded( j, i ) )
        {
          mTiles[j][i].tile->drawLines();
         // mTiles[j][i].tile->drawMFBO();
        }
      }
    }
  }

  glActiveTexture(GL_TEXTURE1);
  glDisable(GL_TEXTURE_2D);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_TEXTURE_2D);

  glColor4f(1,1,1,1);
  glEnable(GL_BLEND);

  if (video.mSupportShaders) {
    Vec4D spec_color(0,0,0,1);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec_color);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 0);
  }

  // unbind hardware buffers
  glBindBuffer(GL_ARRAY_BUFFER, 0);




  glEnable(GL_CULL_FACE);

  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);

  // TEMP: for fucking around with lighting
  for(OpenGL::Light light = GL_LIGHT0; light < GL_LIGHT0 + 8; ++light )
  {
    glLightf(light, GL_CONSTANT_ATTENUATION, l_const);
    glLightf(light, GL_LINEAR_ATTENUATION, l_linear);
    glLightf(light, GL_QUADRATIC_ATTENUATION, l_quadratic);
  }





  // M2s / models
  if( drawmodels)
  {
    ModelManager::resetAnim();

    glEnable(GL_LIGHTING);  //! \todo  Is this needed? Or does this fuck something up?
    for( std::map<int, ModelInstance>::iterator it = mModelInstances.begin(); it != mModelInstances.end(); ++it )
      it->second.draw (draw_fog);

    //drawModelList();
  }




  // WMOs / map objects
  if( drawwmo || mHasAGlobalWMO )
    if (video.mSupportShaders)
    {
      Vec4D spec_color( 1.0f, 1.0f, 1.0f, 1.0f );
      glMaterialfv( GL_FRONT_AND_BACK, GL_SPECULAR, spec_color );
      glMateriali( GL_FRONT_AND_BACK, GL_SHININESS, 10 );

      glLightModeli( GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR );

      for( std::map<int, WMOInstance>::iterator it = mWMOInstances.begin(); it != mWMOInstances.end(); ++it )
        it->second.draw (draw_wmo_doodads, draw_fog);

      spec_color = Vec4D( 0.0f, 0.0f, 0.0f, 1.0f );
      glMaterialfv( GL_FRONT_AND_BACK, GL_SPECULAR, spec_color );
      glMateriali( GL_FRONT_AND_BACK, GL_SHININESS, 0 );
    }
    else
      for( std::map<int, WMOInstance>::iterator it = mWMOInstances.begin(); it != mWMOInstances.end(); ++it )
        it->second.draw (draw_wmo_doodads, draw_fog);

  outdoorLights( true );
  setupFog (draw_fog);

  glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
  glDisable(GL_CULL_FACE);

  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glEnable(GL_LIGHTING);

  // gosh darn alpha blended evil

  OpenGL::SettingsSaver::restore();
  setupFog (draw_fog);

  /*
  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        mTiles[j][i].tile->drawWater();
      }
    }
  }
  */


  glColor4f(1,1,1,1);
  glEnable(GL_BLEND);

  /*
  // temp frustum code
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBegin(GL_TRIANGLES);
  glColor4f(0,1,0,0.5);
  glVertex3fv(camera);
  glVertex3fv(fp - rt * fl * 1.33f - up * fl);
  glVertex3fv(fp + rt * fl * 1.33f - up * fl);
  glColor4f(0,0,1,0.5);
  glVertex3fv(camera);
  fl *= 0.5f;
  glVertex3fv(fp - rt * fl * 1.33f + up * fl);
  glVertex3fv(fp + rt * fl * 1.33f + up * fl);
  glEnd();
  */

  //glColor4f(1,1,1,1);
  glDisable(GL_COLOR_MATERIAL);

  if(drawwater)
  {
    for( int j = 0; j < 64; ++j )
    {
      for( int i = 0; i < 64; ++i )
      {
        if( tileLoaded( j, i ) )
        {
          mTiles[j][i].tile->drawWater();
        }
      }
    }
  }


  ex = camera.x / TILESIZE;
  ez = camera.z / TILESIZE;
}

static const GLuint MapObjName = 1;
static const GLuint DoodadName = 2;
static const GLuint MapTileName = 3;

void World::drawSelection ( int cursorX
                          , int cursorY
                          , bool pOnlyMap
                          , bool draw_wmo_doodads
                          )
{
  glSelectBuffer( sizeof( selectionBuffer ) / sizeof( GLuint ), selectionBuffer );
  glRenderMode( GL_SELECT );

  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();

  GLint viewport[4];
  glGetIntegerv( GL_VIEWPORT, viewport );
  gluPickMatrix( cursorX, viewport[3] - cursorY, 7, 7, viewport );

  video.set3D_select();

  glBindBuffer( GL_ARRAY_BUFFER, 0 );

  gluLookAt( camera.x, camera.y, camera.z, lookat.x, lookat.y, lookat.z, 0, 1, 0 );

  frustum.retrieve();

  glClear( GL_DEPTH_BUFFER_BIT );

  glInitNames();

  glPushName( MapTileName );
  if( drawterrain )
  {
    for( int j = 0; j < 64; ++j )
    {
      for( int i = 0; i < 64; ++i )
      {
        if( tileLoaded( j, i ) )
        {
          mTiles[j][i].tile->drawSelect();
        }
      }
    }
  }
  glPopName();

  if( !pOnlyMap )
  {
    // WMOs / map objects
    if( drawwmo )
    {
      glPushName( MapObjName );
      glPushName( 0 );
      for( std::map<int, WMOInstance>::iterator it = mWMOInstances.begin(); it != mWMOInstances.end(); ++it )
      {
        it->second.drawSelect (draw_wmo_doodads);
      }
      glPopName();
      glPopName();
    }

    // M2s / models
    if( drawmodels )
    {
      ModelManager::resetAnim();

      glPushName( DoodadName );
      glPushName( 0 );
      for( std::map<int, ModelInstance>::iterator it = mModelInstances.begin(); it != mModelInstances.end(); ++it )
      {
        it->second.drawSelect();
      }
      glPopName();
      glPopName();
    }
  }

  getSelection();
}

struct GLNameEntry
{
  GLuint stackSize;
  GLuint nearZ;
  GLuint farZ;
  struct
  {
    GLuint type;
    union
    {
      GLuint dummy;
      GLuint chunk;
    };
    union
    {
      GLuint uniqueId;
      GLuint triangle;
    };
  } stack;
};

void World::getSelection()
{
  GLuint minDist = 0xFFFFFFFF;
  GLNameEntry* minEntry = NULL;
  GLuint hits = glRenderMode( GL_RENDER );

  size_t offset = 0;

  //! \todo Isn't the closest one always the first? Iterating would be worthless then.
  while( hits-- > 0U )
  {
    GLNameEntry* entry = reinterpret_cast<GLNameEntry*>( &selectionBuffer[offset] );

    // We always push { MapObjName | DoodadName | MapTileName }, { 0, 0, MapTile }, { UID, UID, triangle }
    assert( entry->stackSize == 3 );

    if( entry->nearZ < minDist )
    {
      minDist = entry->nearZ;
      minEntry = entry;
    }

    offset += sizeof( GLNameEntry ) / sizeof( GLuint );
  }

  if( minEntry )
  {
    if( minEntry->stack.type == MapObjName || minEntry->stack.type == DoodadName )
    {
      mCurrentSelection = selection_names().findEntry( minEntry->stack.uniqueId );
    }
    else if( minEntry->stack.type == MapTileName )
    {
      mCurrentSelection = selection_names().findEntry( minEntry->stack.chunk );
      mCurrentSelectedTriangle = minEntry->stack.triangle;
    }
  }
}

void World::tick(float dt)
{
  enterTile(ex,ez);

  while (dt > 0.1f) {
    ModelManager::updateEmitters(0.1f);
    dt -= 0.1f;
  }
  ModelManager::updateEmitters(dt);
}

unsigned int World::getAreaID()
{
  const int mtx = camera.x / TILESIZE;
  const int mtz = camera.z / TILESIZE;
  const int mcx = fmod(camera.x, TILESIZE) / CHUNKSIZE;
  const int mcz = fmod(camera.z, TILESIZE) / CHUNKSIZE;

  if((mtx<cx-1) || (mtx>cx+1) || (mtz<cz-1) || (mtz>cz+1))
    return 0;

  MapTile* curTile = mTiles[mtz][mtx].tile;
  if(!curTile)
    return 0;

  MapChunk *curChunk = curTile->getChunk(mcx, mcz);
  if(!curChunk)
    return 0;

  return curChunk->areaID;
}

void World::clearHeight(int id, int x, int z)
{

  // set the Area ID on a tile x,z on all chunks
  for (int j=0; j<16; ++j)
  {
    for (int i=0; i<16; ++i)
    {
      clearHeight(id, x, z, j, i);
    }
  }

  for (int j=0; j<16; ++j)
  {
    for (int i=0; i<16; ++i)
    {
      // set the Area ID on a tile x,z on the chunk cx,cz
      MapTile *curTile;
      curTile = mTiles[z][x].tile;
      if(curTile == 0) return;
      setChanged(z,x);
      MapChunk *curChunk = curTile->getChunk(j, i);
      curChunk->recalcNorms();
    }
  }

}

void World::clearHeight(int /*id*/, int x, int z , int _cx, int _cz)
{
  // set the Area ID on a tile x,z on the chunk cx,cz
  MapTile *curTile;
  curTile = mTiles[z][x].tile;
  if(curTile == 0) return;
  setChanged(z,x);
  MapChunk *curChunk = curTile->getChunk(_cx, _cz);
  if(curChunk == 0) return;

  curChunk->vmin.y = 9999999.0f;
  curChunk->vmax.y = -9999999.0f;
  curChunk->Changed=true;

  for(int i=0; i < mapbufsize; ++i)
  {
    curChunk->mVertices[i].y = 0.0f;

    curChunk->vmin.y = std::min(curChunk->vmin.y,curChunk-> mVertices[i].y);
    curChunk->vmax.y = std::max(curChunk->vmax.y, curChunk->mVertices[i].y);
  }

  glBindBuffer(GL_ARRAY_BUFFER, curChunk->vertices);
  glBufferData(GL_ARRAY_BUFFER, sizeof(curChunk->mVertices), curChunk->mVertices, GL_STATIC_DRAW);
}

void World::clearAllModelsOnADT(int x,int z)
{
  // get the adt
  MapTile *curTile;
  curTile = mTiles[z][x].tile;
  if(curTile == 0) return;
  curTile->clearAllModels();
}

void World::setAreaID(int id, int x,int z)
{
  // set the Area ID on a tile x,z on all chunks
  for (int j=0; j<16; ++j)
  {
    for (int i=0; i<16; ++i)
    {
      setAreaID(id, x, z, j, i);
    }
  }
}

void World::setAreaID(int id, int x, int z , int _cx, int _cz)
{

  // set the Area ID on a tile x,z on the chunk cx,cz
  MapTile *curTile;
  curTile = mTiles[z][x].tile;
  if(curTile == 0) return;
  setChanged(z,x);
  MapChunk *curChunk = curTile->getChunk(_cx, _cz);

  if(curChunk == 0) return;

  curChunk->areaID = id;
}

void World::drawTileMode(float /*ah*/)
{
  glClear(GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glPushMatrix();
  glScalef(zoom,zoom,1.0f);

  glPushMatrix();
  glTranslatef(-camera.x/CHUNKSIZE,-camera.z/CHUNKSIZE,0);

  minX = camera.x/CHUNKSIZE - 2.0f*video.ratio()/zoom;
  maxX = camera.x/CHUNKSIZE + 2.0f*video.ratio()/zoom;
  minY = camera.z/CHUNKSIZE - 2.0f/zoom;
  maxY = camera.z/CHUNKSIZE + 2.0f/zoom;

  glEnableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);

  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        mTiles[j][i].tile->drawTextures (animtime);
      }
    }
  }

  glDisableClientState(GL_COLOR_ARRAY);

  glEnableClientState(GL_NORMAL_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);



  glPopMatrix();
  if (drawlines) {
    glTranslatef(fmod(-camera.x/CHUNKSIZE,16), fmod(-camera.z/CHUNKSIZE,16),0);
  /*  for(int x=-32;x<=48;x++)
    {
      if(x%16==0)
        glColor4f(0.0f,1.0f,0.0f,0.5f);
      else
        glColor4f(1.0f,0.0f,0.0f,0.5f);
      glBegin(GL_LINES);
      glVertex3f(-32.0f,(float)x,-1);
      glVertex3f(48.0f,(float)x,-1);
      glVertex3f((float)x,-32.0f,-1);
      glVertex3f((float)x,48.0f,-1);
      glEnd();
    }*/

    for(float x = -32.0f; x <= 48.0f; x += 1.0f)
    {
      if( static_cast<int>(x) % 16 )
        glColor4f(1.0f,0.0f,0.0f,0.5f);
      else
        glColor4f(0.0f,1.0f,0.0f,0.5f);
      glBegin(GL_LINES);
      glVertex3f(-32.0f,x,-1);
      glVertex3f(48.0f,x,-1);
      glVertex3f(x,-32.0f,-1);
      glVertex3f(x,48.0f,-1);
      glEnd();
    }
  }

  glPopMatrix();

  ex = camera.x / TILESIZE;
  ez = camera.z / TILESIZE;
}

bool World::GetVertex(float x,float z, Vec3D *V)
{
  const int newX = x / TILESIZE;
  const int newZ = z / TILESIZE;

  if( !tileLoaded( newZ, newX ) )
  {
    return false;
  }

  return mTiles[newZ][newX].tile->GetVertex(x, z, V);
}

void World::changeTerrain(float x, float z, float change, float radius, int BrushType)
{
  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            if( mTiles[j][i].tile->getChunk(ty,tx)->changeTerrain(x,z,change,radius,BrushType) )
              setChanged( j, i );
          }
        }
      }
    }
  }

  for( size_t j = 0; j < 64; ++j )
  {
    for( size_t i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            mTiles[j][i].tile->getChunk(ty,tx)->recalcNorms();
          }
        }
      }
    }
  }
}

void World::flattenTerrain(float x, float z, float h, float remain, float radius, int BrushType)
{
  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            if( mTiles[j][i].tile->getChunk(ty,tx)->flattenTerrain(x,z,h,remain,radius,BrushType) )
              setChanged(j,i);
          }
        }
      }
    }
  }

  for( size_t j = 0; j < 64; ++j )
  {
    for( size_t i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            mTiles[j][i].tile->getChunk(ty,tx)->recalcNorms();
          }
        }
      }
    }
  }
}

void World::blurTerrain(float x, float z, float remain, float radius, int BrushType)
{
  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            if( mTiles[j][i].tile->getChunk(ty,tx)->blurTerrain(x, z, remain, radius, BrushType) )
              setChanged(j,i);
          }
        }
      }
    }
  }

  for( size_t j = 0; j < 64; ++j )
  {
    for( size_t i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            mTiles[j][i].tile->getChunk(ty,tx)->recalcNorms();
          }
        }
      }
    }
  }
}

bool World::paintTexture(float x, float z, brush *Brush, float strength, float pressure, OpenGL::Texture* texture)
{
  //const int newX = (int)(x / TILESIZE);
  //const int newZ = (int)(z / TILESIZE);

  //Log << "Painting Textures at " << x << " and " << z;
  bool succ = false;

  for( int j = 0; j < 64; ++j )
  {
    for( int i = 0; i < 64; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            if( mTiles[j][i].tile->getChunk( ty, tx )->paintTexture( x, z, Brush, strength, pressure, texture ) )
            {
              succ |= true;
              setChanged( j, i );
            }
          }
        }
      }
    }
  }
  return succ;
}

void World::eraseTextures(float x, float z)
{
  setChanged(x,z);
  const size_t newX = x / TILESIZE;
  const size_t newZ = z / TILESIZE;
  Log << "Erasing Textures at " << x << " and " << z;
  for( size_t j = newZ - 1; j < newZ + 1; ++j )
  {
    for( size_t i = newX - 1; i < newX + 1; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            MapChunk* chunk = mTiles[j][i].tile->getChunk( ty, tx );
            if( chunk->xbase < x && chunk->xbase + CHUNKSIZE > x && chunk->zbase < z && chunk->zbase + CHUNKSIZE > z )
            {
              chunk->eraseTextures();
            }
          }
        }
      }
    }
  }
}

void World::overwriteTextureAtCurrentChunk(float x, float z, OpenGL::Texture* oldTexture, OpenGL::Texture* newTexture)
{
  setChanged(x,z);
  const size_t newX = x / TILESIZE;
  const size_t newZ = z / TILESIZE;
  Log << "Switching Textures at " << x << " and " << z;
  for( size_t j = newZ - 1; j < newZ + 1; ++j )
  {
    for( size_t i = newX - 1; i < newX + 1; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            MapChunk* chunk = mTiles[j][i].tile->getChunk( ty, tx );
            if( chunk->xbase < x && chunk->xbase + CHUNKSIZE > x && chunk->zbase < z && chunk->zbase + CHUNKSIZE > z )
            {
              chunk->switchTexture(oldTexture, newTexture);
            }
          }
        }
      }
    }
  }
}

void World::addHole( float x, float z )
{
  setChanged(x, z);
  const size_t newX = x / TILESIZE;
  const size_t newZ = z / TILESIZE;

  for( size_t j = newZ - 1; j < newZ + 1; ++j )
  {
    for( size_t i = newX - 1; i < newX + 1; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            MapChunk* chunk = mTiles[j][i].tile->getChunk( ty, tx );
            if( chunk->xbase < x && chunk->xbase + CHUNKSIZE > x && chunk->zbase < z && chunk->zbase + CHUNKSIZE > z )
            {
              int k = ( x - chunk->xbase ) / MINICHUNKSIZE;
              int l = ( z - chunk->zbase ) / MINICHUNKSIZE;
              chunk->addHole( k, l );
            }
          }
        }
      }
    }
  }
}

void World::removeHole( float x, float z )
{
  setChanged(x, z);
  const size_t newX = x / TILESIZE;
  const size_t newZ = z / TILESIZE;

  for( size_t j = newZ - 1; j < newZ + 1; ++j )
  {
    for( size_t i = newX - 1; i < newX + 1; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( size_t ty = 0; ty < 16; ++ty )
        {
          for( size_t tx = 0; tx < 16; ++tx )
          {
            MapChunk* chunk = mTiles[j][i].tile->getChunk( ty, tx );
            if( chunk->xbase < x && chunk->xbase + CHUNKSIZE > x && chunk->zbase < z && chunk->zbase + CHUNKSIZE > z )
            {
              int k = ( x - chunk->xbase ) / MINICHUNKSIZE;
              int l = ( z - chunk->zbase ) / MINICHUNKSIZE;
              chunk->removeHole( k, l );
            }
          }
        }
      }
    }
  }
}

void World::saveMap()
{
  //! \todo  Output as BLP.
  unsigned char image[256*256*3];
  MapTile *ATile;
  FILE *fid;
  glEnable(GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glReadBuffer(GL_BACK);

  minX=-64*16;
  maxX=64*16;
  minY=-64*16;
  maxY=64*16;

  glEnableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);

  for(int y=0;y<64;y++)
  {
    for(int x=0;x<64;x++)
    {
      if( !( mTiles[y][x].flags & 1 ) )
      {
        continue;
      }

      ATile=loadTile(x,y);
      glClear(GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT);

      glPushMatrix();
      glScalef(0.08333333f,0.08333333f,1.0f);

      //glTranslatef(-camera.x/CHUNKSIZE,-camera.z/CHUNKSIZE,0);
      glTranslatef( x * -16.0f - 8.0f, y * -16.0f - 8.0f, 0.0f );

      ATile->drawTextures (animtime);
      glPopMatrix();
      glReadPixels(video.xres()/2-128,video.yres()/2-128,256,256,GL_RGB,GL_UNSIGNED_BYTE,image);
      video.flip();
    std::stringstream ss;
    ss << basename.c_str() << "_map_" << x << "_" << y << ".raw";
    fid=fopen(ss.str().c_str(),"wb");
      fwrite(image,256*3,256,fid);
      fclose(fid);
    }
  }

  glDisableClientState(GL_COLOR_ARRAY);

  glEnableClientState(GL_NORMAL_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

void World::deleteModelInstance( int pUniqueID )
{
  std::map<int, ModelInstance>::iterator it = mModelInstances.find( pUniqueID );
  setChanged( it->second.pos.x, it->second.pos.z );
  mModelInstances.erase( it );
  ResetSelection();
}

void World::deleteWMOInstance( int pUniqueID )
{
  std::map<int, WMOInstance>::iterator it = mWMOInstances.find( pUniqueID );
  setChanged( it->second.pos.x, it->second.pos.z );
  mWMOInstances.erase( it );
  ResetSelection();
}

void World::addModel( nameEntry entry, Vec3D newPos )
{
  if( entry.type == eEntry_Model )
    addM2( entry.data.model->model, newPos );
  else if( entry.type == eEntry_WMO )
    addWMO( entry.data.wmo->wmo, newPos );
}

void World::addM2( Model *model, Vec3D newPos )
{
  int temp = 0;
  if  (mModelInstances.empty()) {
    temp = 0;
  }
  else{
    temp = mModelInstances.rbegin()->first + 1;
  }
  const int lMaxUID = temp;
//  ( ( mModelInstances.empty() ? 0 : mModelInstances.rbegin()->first + 1 ),
//                           ( mWMOInstances.empty() ? 0 : mWMOInstances.rbegin()->first + 1 ) );

  ModelInstance newModelis (this, model);
  newModelis.nameID = -1;
  newModelis.d1 = lMaxUID;
  newModelis.pos = newPos;
  newModelis.sc = 1;
  if(Settings::getInstance()->copy_rot)
  {
    newModelis.dir.y += (rand() % 360 + 1);
  }

  if(Settings::getInstance()->copy_tile)
  {
    newModelis.dir.x += (rand() % 5 + 1);
    newModelis.dir.z += (rand() % 5 + 1);
  }

  if(Settings::getInstance()->copy_size)
  {
    newModelis.sc *= misc::randfloat( 0.9f, 1.1f );
  }

  mModelInstances.insert( std::pair<int,ModelInstance>( lMaxUID, newModelis ));
  setChanged(newPos.x,newPos.z);
}

void World::addWMO( WMO *wmo, Vec3D newPos )
{
  const int lMaxUID = std::max( ( mModelInstances.empty() ? 0 : mModelInstances.rbegin()->first + 1 ),
                           ( mWMOInstances.empty() ? 0 : mWMOInstances.rbegin()->first + 1 ) );

  WMOInstance newWMOis(this, wmo);
  newWMOis.pos = newPos;
  newWMOis.mUniqueID = lMaxUID;
  mWMOInstances.insert( std::pair<int,WMOInstance>( lMaxUID, newWMOis ));
  setChanged(newPos.x,newPos.z);
}

void World::setChanged(float x, float z)
{
  // change the changed flag of the map tile
  int row =  misc::FtoIround((x-(TILESIZE/2))/TILESIZE);
  int column =  misc::FtoIround((z-(TILESIZE/2))/TILESIZE);
  if( row >= 0 && row <= 64 && column >= 0 && column <= 64 )
    if( mTiles[column][row].tile )
      mTiles[column][row].tile->changed = true;
}

void World::setChanged(int x, int z)
{
  // change the changed flag of the map tile
  if( mTiles[x][z].tile )
    mTiles[x][z].tile->changed = true;
}

void World::unsetChanged(int x, int z)
{
  // change the changed flag of the map tile
  if( mTiles[x][z].tile )
    mTiles[x][z].tile->changed = false;
}

bool World::getChanged(int x, int z) const
{
  if(mTiles[x][z].tile)
    return mTiles[x][z].tile->changed;
  else return false;
}

void World::setFlag( bool to, float x, float z)
{
  // set the inpass flag to selected chunk
  setChanged(x, z);
  const int newX = x / TILESIZE;
  const int newZ = z / TILESIZE;

  for( int j = newZ - 1; j < newZ + 1; ++j )
  {
    for( int i = newX - 1; i < newX + 1; ++i )
    {
      if( tileLoaded( j, i ) )
      {
        for( int ty = 0; ty < 16; ++ty )
        {
          for( int tx = 0; tx < 16; ++tx )
          {
            MapChunk* chunk = mTiles[j][i].tile->getChunk( ty, tx );
            if( chunk->xbase < x && chunk->xbase + CHUNKSIZE > x && chunk->zbase < z && chunk->zbase + CHUNKSIZE > z )
            {
              chunk->setFlag(to);
            }
          }
        }
      }
    }
  }
}

unsigned int World::getMapID()
{
  return mMapId;
}


void World::moveHeight(int id, int x, int z)
{

  // set the Area ID on a tile x,z on all chunks
  for (int j=0; j<16; ++j)
  {
    for (int i=0; i<16; ++i)
    {
      moveHeight(id, x, z, j, i);
    }
  }

  for (int j=0; j<16; ++j)
  {
    for (int i=0; i<16; ++i)
    {
      // set the Area ID on a tile x,z on the chunk cx,cz
      MapTile *curTile;
      curTile = mTiles[z][x].tile;
      if(curTile == 0) return;
      setChanged(z,x);
      MapChunk *curChunk = curTile->getChunk(j, i);
      curChunk->recalcNorms();
    }
  }

}

void World::moveHeight(int /*id*/, int x, int z , int _cx, int _cz)
{
  // set the Area ID on a tile x,z on the chunk cx,cz
  MapTile *curTile;
  curTile = mTiles[z][x].tile;
  if(curTile == 0) return;
  setChanged(z,x);
  MapChunk *curChunk = curTile->getChunk(_cx, _cz);
  if(curChunk == 0) return;

  curChunk->vmin.y = 9999999.0f;
  curChunk->vmax.y = -9999999.0f;
  curChunk->Changed = true;

  float heightDelta = 0.0f;
  nameEntry *selection = GetCurrentSelection();

  if(selection)
    if(selection->type == eEntry_MapChunk)
    {
      // chunk selected
      heightDelta = camera.y - selection->data.mapchunk->py;
    }

  if( heightDelta * heightDelta <= 0.1f ) return;

  for(int i=0; i < mapbufsize; ++i)
  {
    curChunk->mVertices[i].y = curChunk->mVertices[i].y + heightDelta;

    curChunk->vmin.y = std::min(curChunk->vmin.y,curChunk-> mVertices[i].y);
    curChunk->vmax.y = std::max(curChunk->vmax.y, curChunk->mVertices[i].y);
  }

  glBindBuffer(GL_ARRAY_BUFFER, curChunk->vertices);
  glBufferData(GL_ARRAY_BUFFER, sizeof(curChunk->mVertices), curChunk->mVertices, GL_STATIC_DRAW);


}

void World::setBaseTexture( int x, int z )
{
  if( !UITexturingGUI::getSelectedTexture() ) return;
  MapTile *curTile;
  curTile = mTiles[z][x].tile;
  if(curTile == 0) return;

  // clear all textures on the adt and set selected texture as base texture
  for (int j=0; j<16; ++j)
  {
    for (int i=0; i<16; ++i)
    {
      MapChunk *curChunk = curTile->getChunk(j, i);
      curChunk->eraseTextures();
      curChunk->addTexture( UITexturingGUI::getSelectedTexture() );
      UITexturingGUI::getSelectedTexture()->addReference();
    }
  }
}


void World::saveWDT()
{
   // int lCurrentPosition = 0;
    //sExtendableArray lWDTFile = sExtendableArray();
   // lWDTFile.Extend( 8 + 0x4 );
   // SetChunkHeader( lWDTFile, lCurrentPosition, 'MPHD', 4 );

   // MPQFile f( "test.WDT" );
   // f.setBuffer( lWDTFile.GetPointer<uint8_t>(), lWDTFile.mSize );
   // f.SaveFile();
   // f.close();
}