/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2012 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include <osgEarthUtil/LinearLineOfSight>
#include <osgEarth/TerrainEngineNode>
#include <osgSim/LineOfSight>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

using namespace osgEarth;
using namespace osgEarth::Util;

namespace
{
    class TerrainChangedCallback : public osgEarth::TerrainCallback
    {
    public:
        TerrainChangedCallback( LinearLineOfSightNode* los ):
          _los(los)
        {
        }

        virtual void onTileAdded(const osgEarth::TileKey& tileKey, osg::Node* terrain, TerrainCallbackContext&)
        {
            _los->terrainChanged( tileKey, terrain );
        }

    private:
        LinearLineOfSightNode* _los;
    }; 


#if 0
    bool getRelativeWorld(double x, double y, double relativeHeight, MapNode* mapNode, osg::Vec3d& world )
    {
        GeoPoint mapPoint(mapNode->getMapSRS(), x, y);
        osg::Vec3d pos;
        mapNode->getMap()->toWorldPoint(mapPoint, pos);

        osg::Vec3d up(0,0,1);
        const osg::EllipsoidModel* em = mapNode->getMap()->getProfile()->getSRS()->getEllipsoid();
        if (em)
        {
            up = em->computeLocalUpVector( world.x(), world.y(), world.z());
        }    
        up.normalize();

        double segOffset = 50000;

        osg::Vec3d start = pos + (up * segOffset);
        osg::Vec3d end = pos - (up * segOffset);
        
        osgUtil::LineSegmentIntersector* i = new osgUtil::LineSegmentIntersector( start, end );
        
        osgUtil::IntersectionVisitor iv;    
        iv.setIntersector( i );
        mapNode->getTerrainEngine()->accept( iv );

        osgUtil::LineSegmentIntersector::Intersections& results = i->getIntersections();
        if ( !results.empty() )
        {
            const osgUtil::LineSegmentIntersector::Intersection& result = *results.begin();
            world = result.getWorldIntersectPoint();
            world += up * relativeHeight;
            return true;
        }
        return false;    
    }
#endif


    osg::Vec3d getNodeCenter(osg::Node* node)
    {
        osg::NodePathList nodePaths = node->getParentalNodePaths();
        if ( nodePaths.empty() )
            return node->getBound().center();

        osg::NodePath path = nodePaths[0];

        osg::Matrixd localToWorld = osg::computeLocalToWorld( path );
        osg::Vec3d center = osg::Vec3d(0,0,0) * localToWorld;

        // if the tether node is a MT, we are set. If it's not, we need to get the
        // local bound and add its translation to the localToWorld. We cannot just use
        // the bounds directly because they are single precision (unless you built OSG
        // with double-precision bounding spheres, which you probably did not :)
        if ( !dynamic_cast<osg::MatrixTransform*>( node ) )
        {
            const osg::BoundingSphere& bs = node->getBound();
            center += bs.center();
        }   
        return center;
    }
}

//------------------------------------------------------------------------


LinearLineOfSightNode::LinearLineOfSightNode(osgEarth::MapNode *mapNode):
LineOfSightNode(),
_mapNode(mapNode),
_start(0,0,0),
_end(0,0,0),
_hit(0,0,0),
_hasLOS( true ),
_clearNeeded( false ),
_goodColor(0.0f, 1.0f, 0.0f, 1.0f),
_badColor(1.0f, 0.0f, 0.0f, 1.0f),
_displayMode( LineOfSight::MODE_SPLIT ),
_startAltitudeMode( ALTMODE_ABSOLUTE ),
_endAltitudeMode( ALTMODE_ABSOLUTE ),
_terrainOnly( false )
{
    compute(getNode());
    subscribeToTerrain();
    setNumChildrenRequiringUpdateTraversal( 1 );
}


LinearLineOfSightNode::LinearLineOfSightNode(osgEarth::MapNode* mapNode, 
                                             const osg::Vec3d& start, 
                                             const osg::Vec3d& end):
LineOfSightNode(),
_mapNode(mapNode),
_start(start),
_end(end),
_hit(0,0,0),
_hasLOS( true ),
_clearNeeded( false ),
_goodColor(0.0f, 1.0f, 0.0f, 1.0f),
_badColor(1.0f, 0.0f, 0.0f, 1.0f),
_displayMode( LineOfSight::MODE_SPLIT ),
_startAltitudeMode( ALTMODE_ABSOLUTE ),
_endAltitudeMode( ALTMODE_ABSOLUTE ),
_terrainOnly( false )
{
    compute(getNode());    
    subscribeToTerrain();
    setNumChildrenRequiringUpdateTraversal( 1 );
}


void
LinearLineOfSightNode::subscribeToTerrain()
{
    _terrainChangedCallback = new TerrainChangedCallback( this );
    _mapNode->getTerrain()->addTerrainCallback( _terrainChangedCallback.get() );        
}

LinearLineOfSightNode::~LinearLineOfSightNode()
{
    //Unsubscribe to the terrain callback
    _mapNode->getTerrain()->removeTerrainCallback( _terrainChangedCallback.get() );
}

void
LinearLineOfSightNode::terrainChanged( const osgEarth::TileKey& tileKey, osg::Node* terrain )
{
    OE_DEBUG << "LineOfSightNode::terrainChanged" << std::endl;
    //Make a temporary group that contains both the old MapNode as well as the new incoming terrain.
    //Because this function is called from the database pager thread we need to include both b/c 
    //the new terrain isn't yet merged with the new terrain.
    osg::ref_ptr < osg::Group > group = new osg::Group;
    group->addChild( terrain );
    group->addChild( getNode() );
    compute( group, true );
}

const osg::Vec3d&
LinearLineOfSightNode::getStart() const
{
    return _start;
}

void
LinearLineOfSightNode::setStart(const osg::Vec3d& start)
{
    if (_start != start)
    {
        _start = start;
        compute(getNode());
    }
}

const osg::Vec3d&
LinearLineOfSightNode::getEnd() const
{
    return _end;
}

void
LinearLineOfSightNode::setEnd(const osg::Vec3d& end)
{
    if (_end != end)
    {
        _end = end;
        compute(getNode());
    }
}

const osg::Vec3d&
LinearLineOfSightNode::getStartWorld() const
{
    return _startWorld;
}

const osg::Vec3d&
LinearLineOfSightNode::getEndWorld() const
{
    return _endWorld;
}

const osg::Vec3d&
LinearLineOfSightNode::getHitWorld() const
{
    return _hitWorld;
}

const osg::Vec3d&
LinearLineOfSightNode::getHit() const
{
    return _hit;
}

bool
LinearLineOfSightNode::getHasLOS() const
{
    return _hasLOS;
}

AltitudeMode
LinearLineOfSightNode::getStartAltitudeMode() const
{
    return _startAltitudeMode;
}

AltitudeMode
LinearLineOfSightNode::getEndAltitudeMode() const
{
    return _endAltitudeMode;
}

void
LinearLineOfSightNode::setStartAltitudeMode( AltitudeMode mode )
{
    if (_startAltitudeMode != mode)
    {
        _startAltitudeMode = mode;
        compute(getNode());
    }
}

void
LinearLineOfSightNode::setEndAltitudeMode( AltitudeMode mode )
{
    if (_endAltitudeMode != mode)
    {
        _endAltitudeMode = mode;
        compute(getNode());
    }
}

void
LinearLineOfSightNode::addChangedCallback( LOSChangedCallback* callback )
{
    _changedCallbacks.push_back( callback );
}

void
LinearLineOfSightNode::removeChangedCallback( LOSChangedCallback* callback )
{
    LOSChangedCallbackList::iterator i = std::find( _changedCallbacks.begin(), _changedCallbacks.end(), callback);
    if (i != _changedCallbacks.end())
    {
        _changedCallbacks.erase( i );
    }    
}


bool
LinearLineOfSightNode::computeLOS( osgEarth::MapNode* mapNode, const osg::Vec3d& start, const osg::Vec3d& end, AltitudeMode altitudeMode, osg::Vec3d& hit )
{
    const SpatialReference* mapSRS = mapNode->getMapSRS();

    // convert endpoint to world coordinates:
    osg::Vec3d startWorld, endWorld;
    GeoPoint(mapSRS, start, altitudeMode).toWorld( startWorld, mapNode->getTerrain() );
    GeoPoint(mapSRS, end,   altitudeMode).toWorld( endWorld,   mapNode->getTerrain() );

#if 0
    if (altitudeMode == ALTMODE_ABSOLUTE)
    {
        mapNode->getMap()->toWorldPoint( GeoPoint(mapSRS, start, ALTMODE_ABSOLUTE), startWorld );
        mapNode->getMap()->toWorldPoint( GeoPoint(mapSRS, end,   ALTMODE_ABSOLUTE), endWorld );
    }
    else
    {
        getRelativeWorld(start.x(), start.y(), start.z(), mapNode, startWorld);
        getRelativeWorld(end.x(), end.y(), end.z(), mapNode, endWorld);
    }
#endif
    
    osgSim::LineOfSight los;
    los.setDatabaseCacheReadCallback(0);
    unsigned int index = los.addLOS(startWorld, endWorld);
    los.computeIntersections(mapNode);
    osgSim::LineOfSight::Intersections hits = los.getIntersections(0);    
    if (hits.size() > 0)
    {
        osg::Vec3d hitWorld = *hits.begin();
        GeoPoint mapHit;
        mapHit.fromWorld( mapNode->getMapSRS(), hitWorld );
        //mapNode->getMap()->worldPointToMapPoint(hitWorld, mapHit);
        hit = mapHit.vec3d();
        return false;
    }
    return true;
}



void
LinearLineOfSightNode::compute(osg::Node* node, bool backgroundThread)
{
    if (_start != _end)
    {
      const SpatialReference* mapSRS = _mapNode->getMapSRS();

      //Computes the LOS and redraws the scene
      GeoPoint(mapSRS, _start, _startAltitudeMode).toWorld( _startWorld, _mapNode->getTerrain() );
      GeoPoint(mapSRS, _end,   _endAltitudeMode).toWorld( _endWorld, _mapNode->getTerrain() );

#if 0
      if (_startAltitudeMode == ALTMODE_ABSOLUTE)
          _mapNode->getMap()->toWorldPoint( GeoPoint(mapSRS,_start,ALTMODE_ABSOLUTE), _startWorld );
      else
          getRelativeWorld(_start.x(), _start.y(), _start.z(), _mapNode.get(), _startWorld);

      if (_endAltitudeMode == ALTMODE_ABSOLUTE)
          _mapNode->getMap()->toWorldPoint( GeoPoint(mapSRS,_end,ALTMODE_ABSOLUTE), _endWorld );
      else
          getRelativeWorld(_end.x(), _end.y(), _end.z(), _mapNode.get(), _endWorld);
#endif
      
      osgSim::LineOfSight los;
      los.setDatabaseCacheReadCallback(0);
      unsigned int index = los.addLOS(_startWorld, _endWorld);
      los.computeIntersections(node);
      osgSim::LineOfSight::Intersections hits = los.getIntersections(0);    
      if (hits.size() > 0)
      {
          _hasLOS = false;
          _hitWorld = *hits.begin();
          GeoPoint mapHit;
          mapHit.fromWorld( _mapNode->getMapSRS(), _hitWorld );
          //_mapNode->getMap()->worldPointToMapPoint( _hitWorld, mapHit);
          _hit = mapHit.vec3d();
      }
      else
      {
          _hasLOS = true;
      }
    }

    draw(backgroundThread);

    for( LOSChangedCallbackList::iterator i = _changedCallbacks.begin(); i != _changedCallbacks.end(); i++ )
    {
        i->get()->onChanged();
    }	
}

void
LinearLineOfSightNode::draw(bool backgroundThread)
{
    osg::MatrixTransform* mt = 0L;

    if (_start != _end)
    {
        osg::Geometry* geometry = new osg::Geometry;
        geometry->setUseVertexBufferObjects(true);

        osg::Vec3Array* verts = new osg::Vec3Array();
        verts->reserve(4);
        geometry->setVertexArray( verts );

        osg::Vec4Array* colors = new osg::Vec4Array();
        colors->reserve( 4 );

        geometry->setColorArray( colors );
        geometry->setColorBinding(osg::Geometry::BIND_PER_VERTEX);

        if (_hasLOS)
        {
            verts->push_back( _startWorld - _startWorld );
            verts->push_back( _endWorld   - _startWorld );
            colors->push_back( _goodColor );
            colors->push_back( _goodColor );
        }
        else
        {
            if (_displayMode == LineOfSight::MODE_SINGLE)
            {
                verts->push_back( _startWorld - _startWorld );
                verts->push_back( _endWorld - _startWorld );
                colors->push_back( _badColor );
                colors->push_back( _badColor );
            }
            else if (_displayMode == LineOfSight::MODE_SPLIT)
            {
                verts->push_back( _startWorld - _startWorld );
                colors->push_back( _goodColor );
                verts->push_back( _hitWorld   - _startWorld );
                colors->push_back( _goodColor );

                verts->push_back( _hitWorld   - _startWorld );
                colors->push_back( _badColor );
                verts->push_back( _endWorld   - _startWorld );
                colors->push_back( _badColor );
            }
        }

        geometry->addPrimitiveSet( new osg::DrawArrays( GL_LINES, 0, verts->size()) );        

        osg::Geode* geode = new osg::Geode;
        geode->addDrawable( geometry );

        mt = new osg::MatrixTransform;
        mt->setMatrix(osg::Matrixd::translate(_startWorld));
        mt->addChild(geode);  

        getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    }


    if (!backgroundThread)
    {
        //Remove all children from this group
        removeChildren(0, getNumChildren());

        if (mt)
          addChild( mt );
    }
    else
    {
        _clearNeeded = true;
        _pendingNode = mt;
    }
}

void
LinearLineOfSightNode::setGoodColor( const osg::Vec4f &color )
{
    if (_goodColor != color)
    {
        _goodColor = color;
        draw();
    }
}

const osg::Vec4f&
LinearLineOfSightNode::getGoodColor() const
{
    return _goodColor;
}

void
LinearLineOfSightNode::setBadColor( const osg::Vec4f &color )
{
    if (_badColor != color)
    {
        _badColor = color;
        draw();
    }
}

const osg::Vec4f&
LinearLineOfSightNode::getBadColor() const
{
    return _badColor;
}

LineOfSight::DisplayMode
LinearLineOfSightNode::getDisplayMode() const
{
    return _displayMode;
}

void
LinearLineOfSightNode::setDisplayMode( LineOfSight::DisplayMode displayMode )
{
    if (_displayMode != displayMode)
    {
        _displayMode = displayMode;
        draw();
    }
}

bool
LinearLineOfSightNode::getTerrainOnly() const
{
    return _terrainOnly;
}

void
LinearLineOfSightNode::setTerrainOnly( bool terrainOnly )
{
    if (_terrainOnly != terrainOnly)
    {
        _terrainOnly = terrainOnly;
        compute(getNode());
    }
}

osg::Node*
LinearLineOfSightNode::getNode()
{
    if (_terrainOnly) return _mapNode->getTerrainEngine();
    return _mapNode.get();
}

void
LinearLineOfSightNode::traverse(osg::NodeVisitor& nv)
{
    if (nv.getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR)
    {
        if (_pendingNode.valid() || _clearNeeded)
        {
            removeChildren(0, getNumChildren());

            if (_pendingNode.valid())
              addChild( _pendingNode.get());

            _pendingNode = 0;
            _clearNeeded = false;
        }
    }
    osg::Group::traverse(nv);
}

/**********************************************************************/
LineOfSightTether::LineOfSightTether(osg::Node* startNode, osg::Node* endNode):
_startNode(startNode),
_endNode(endNode)
{
}

void 
LineOfSightTether::operator()(osg::Node* node, osg::NodeVisitor* nv)
{
    if (nv->getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR)
    {
        LinearLineOfSightNode* los = static_cast<LinearLineOfSightNode*>(node);

        if (_startNode.valid())
        {
            osg::Vec3d worldStart = getNodeCenter(_startNode);

            //Convert to mappoint since that is what LOS expects
            GeoPoint mapStart;
            mapStart.fromWorld( los->getMapNode()->getMapSRS(), worldStart );
            //los->getMapNode()->getMap()->worldPointToMapPoint( worldStart, mapStart );
            los->setStart( mapStart.vec3d() );
        }

        if (_endNode.valid())
        {
            osg::Vec3d worldEnd = getNodeCenter( _endNode );

            //Convert to mappoint since that is what LOS expects
            GeoPoint mapEnd;
            mapEnd.fromWorld( los->getMapNode()->getMapSRS(), worldEnd );
            //los->getMapNode()->getMap()->worldPointToMapPoint( worldEnd, mapEnd );
            los->setEnd( mapEnd.vec3d() );
        }
    }
    traverse(node, nv);
}


/**********************************************************************/

namespace 
{
    class LOSDraggerCallback : public Dragger::PositionChangedCallback
    {
    public:
        LOSDraggerCallback(LinearLineOfSightNode* los, bool start):
          _los(los),
          _start(start)
          {      
          }

          virtual void onPositionChanged(const Dragger* sender, const osgEarth::GeoPoint& position)
          {   
              GeoPoint location(position);
              if ((_start ? _los->getStartAltitudeMode() : _los->getEndAltitudeMode()) == ALTMODE_RELATIVE)
              {
                  double z = _start ? _los->getStart().z() : _los->getEnd().z();
                  location.z() = z;              
              }

              if (_start)
              {
                  _los->setStart( location.vec3d() );
              }
              else
              {
                  _los->setEnd( location.vec3d() );
              }
          }

          
          LinearLineOfSightNode* _los;
          bool _start;
    };
}

/**********************************************************************/

namespace
{
    struct LOSUpdateDraggersCallback : public LOSChangedCallback
    {
    public:
        LOSUpdateDraggersCallback( LinearLineOfSightEditor * editor ):
          _editor( editor )
        {

        }
        virtual void onChanged()
        {
            _editor->updateDraggers();
        }

        LinearLineOfSightEditor *_editor;
    };
}

LinearLineOfSightEditor::LinearLineOfSightEditor(LinearLineOfSightNode* los):
_los(los)
{

    _startDragger  = new SphereDragger( _los->getMapNode());
    _startDragger->addPositionChangedCallback(new LOSDraggerCallback(_los, true ) );    
    static_cast<SphereDragger*>(_startDragger)->setColor(osg::Vec4(0,0,1,0));
    addChild(_startDragger);

    _endDragger = new SphereDragger( _los->getMapNode());
    static_cast<SphereDragger*>(_endDragger)->setColor(osg::Vec4(0,0,1,0));
    _endDragger->addPositionChangedCallback(new LOSDraggerCallback(_los, false ) );

    addChild(_endDragger);

    _callback = new LOSUpdateDraggersCallback( this );
    _los->addChangedCallback( _callback.get() );

    updateDraggers();
}

LinearLineOfSightEditor::~LinearLineOfSightEditor()
{
    _los->removeChangedCallback( _callback.get() );
}

void
LinearLineOfSightEditor::updateDraggers()
{
    osg::Vec3d start = _los->getStartWorld();           
    GeoPoint startMap;
    startMap.fromWorld(_los->getMapNode()->getMapSRS(), start);
    _startDragger->setPosition( startMap, false );

    osg::Vec3d end = _los->getEndWorld();           
    GeoPoint endMap;
    endMap.fromWorld(_los->getMapNode()->getMapSRS(), end);    
    _endDragger->setPosition( endMap, false );
}
