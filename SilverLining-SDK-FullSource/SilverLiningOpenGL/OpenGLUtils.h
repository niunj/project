#pragma once

namespace SilverLining
{
	class OpenGLUtils
	{
	public:
		static void CheckError(int line);
		static void ClearErrors();
		static bool QueryError();

		static bool enableDebugOutput;

	};
}