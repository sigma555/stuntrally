#ifndef SH_PLATFORM_H
#define SH_PLATFORM_H

#include <string>

#include <boost/shared_ptr.hpp>

#include "Language.hpp"
#include "PropertySet.hpp"

namespace sh
{
	class Factory;

	// These classes are supposed to be filled by the platform implementation
	class VertexProgram
	{
	};

	class FragmentProgram
	{
	};

	class GeometryProgram
	{
	};

	class TextureUnitState : public PropertySet
	{
	};

	class Pass : public PropertySet
	{
	public:
		virtual TextureUnitState createTextureUnitState () = 0;
		virtual void assignVertexProgram (const VertexProgram& program) = 0;
		virtual void assignFragmentProgram (const FragmentProgram& program) = 0;
		virtual void assignGeometryProgram(const GeometryProgram& program) = 0;
	};

	class Material : public PropertySet
	{
	public:
		virtual boost::shared_ptr<Pass> createPass () = 0;
	};

	class Platform
	{
	public:
		Platform ();
		virtual ~Platform ();

		virtual boost::shared_ptr<Material> createMaterial (const std::string& name) = 0;

		virtual boost::shared_ptr<VertexProgram> createVertexProgram (
			const std::string& name, const std::string& entryPoint,
			const std::string& source, Language lang) = 0;
		virtual boost::shared_ptr<FragmentProgram> createFragmentProgram (
			const std::string& name, const std::string& entryPoint,
			const std::string& source, Language lang) = 0;
		virtual boost::shared_ptr<GeometryProgram> createGeometryProgram (
			const std::string& name, const std::string& entryPoint,
			const std::string& source, Language lang) = 0;

		friend class Factory;

	protected:
		/**
		 * this will be \a true if the platform supports a listener that notifies the system
		 * whenever a material is requested for rendering. if this is supported, shaders can be
		 * compiled on-demand when needed (and not earlier)
		 */
		virtual bool supportsMaterialQueuedListener ();

		/**
		 * fire event: material requested for rendering
		 * @param name material name
		 */
		void fireMaterialRequested (const std::string& name);

	private:
		Factory* mFactory;

		void setFactory(Factory* factory);
	};
}

#endif
