#pragma once

namespace hpr {


struct CmdStream;


class CommandEmitter
{
public:

	virtual ~CommandEmitter() = default;

	virtual void set_command_stream(CmdStream& stream) = 0;
};

} // hpr

