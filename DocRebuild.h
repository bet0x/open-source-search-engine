//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#ifndef FX_DOCREBUILD_H
#define FX_DOCREBUILD_H

#include "DocProcess.h"

class DocRebuild : public DocProcess {
public:
	DocRebuild(const char *filename, bool isUrl);

	DocProcessDocItem* createDocItem(DocProcess *docProcess, const std::string &key, uint32_t firstIp, int64_t lastPos) override;
	void updateXmldoc(XmlDoc *xmlDoc) override;
	void processDocItem(DocProcessDocItem *processDocItem) override;
	int64_t getMaxPending() const override;
	int64_t getDelayMs() const override;
};

extern DocRebuild g_docRebuild;
extern DocRebuild g_docRebuildUrl;

#endif //FX_DOCREBUILD_H
