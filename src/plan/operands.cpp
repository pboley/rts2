/*
 * Scripting operands.
 * Copyright (C) 2009 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "operands.h"

using namespace rts2operands;

Operand *OperandsSet::parseOperand (std::string str)
{
	// let' see what we have as an operand..
	std::string::iterator iter = str.begin ();
	while (iter != str.end () && isspace (*iter))
		iter++;
	if (iter == str.end ())
	  	throw ParsingError ("Empty string");
	// start as number..
	if ((*iter >= '0' && *iter <= '9') || *iter == '-' || *iter == '+' || *iter == '.')
	{
		// parse as string..
		double op, mul = nan ("f");
		// look what is the last string..
		std::string::iterator it_end = --str.end ();
		while (isspace (*it_end))
			it_end--;
		if (*it_end == 'm')
		  	mul = 1/60.0;
		else if (*it_end == 's')
			mul = 1/3600.0;
		else if (*it_end == 'h')
			mul = 15;
		// eats units specifications
		if (isnan (mul))
			mul = 1;
		else
			str = str.substr (0, it_end - str.begin ());
		std::istringstream _is (str);
		_is >> op;
		if (_is.fail () || !_is.eof())
			return new String(str);
		return new Number (op * mul);
	}
	else
	{
		int start = iter - str.begin ();
		while (iter != str.end() && isalnum (*iter))
			iter++;
		// see what we get..
		std::string name = str.substr (start, iter - str.begin () - start);
		if (name == "rand")
		{
			// get two parameters as operands..
			std::string ops = str.substr (start + 4);
			OperandsSet twoOps;
			twoOps.parse (ops);
			if (twoOps.size () != 2)
				throw ParsingError ("Invalid number of parameters - expecting two:" + ops);
			Operand *ret = new RandomNumber (twoOps[0], twoOps[1]);
			// do not delete operands!
			twoOps.clear ();
			return ret;
		}
		else
		{
			if (iter != str.end ())
				throw ParsingError ("Cannot find function with name " + name);
			return new String (name);
		}
	}
}

void OperandsSet::parse (std::string str)
{
	// find operators separators..
	bool bracked = false;
	int simple_braces = 0;
	int curved_braces = 0;
	enum {NONE, SIMPLE, DOUBLE} quotes = NONE;
	int start = 0;
	for (std::string::iterator iter = str.begin(); iter != str.end(); iter++)
	{
		if (quotes != NONE)
		{
			if (*iter == '\'' && quotes == SIMPLE)
				quotes = NONE;
			if (*iter == '"' && quotes == DOUBLE)
				quotes = NONE;
		}
		else
		{
			if (*iter == '(')
			{
				if (iter == str.begin ())
				{
					start++;
					bracked = true;
				}
				simple_braces++;
			}
			if (*iter == ')')
			{
				if (simple_braces > 0)
					simple_braces --;
				else
					throw ParsingError ("too many closing simple braces - )");
			}
			if (*iter == '{')
			  	curved_braces++;
			if (*iter == '}')
			{
				if (curved_braces > 0)
					curved_braces --;
				else
					throw ParsingError ("too many closing curved braces - }");
			}
			if (*iter == '\'')
				quotes = SIMPLE;
			if (*iter == '"')
				quotes = DOUBLE;
			if (curved_braces == 0 && quotes == NONE &&
			  	((*iter == ',' && simple_braces == 1) || (simple_braces == 0 && bracked)))
			{
				std::string ops = str.substr (start, iter - str.begin () - start);
				push_back (parseOperand (ops));
				start = iter - str.begin () + 1;
			}
		}
	}
	if (bracked == false)
	{
		// push back single operator
		push_back (parseOperand (str));
	}
}
