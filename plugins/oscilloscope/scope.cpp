/*
	 Copyright (C) 2011 Georgia Institute of Technology, University of Utah, Weill Cornell Medical College

	 This program is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation, either version 3 of the License, or
	 (at your option) any later version.

	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <QtGui>

#include <rt.h>
#include <debug.h>
#include <cmath>
#include <stdlib.h>
#include <main_window.h>

#include "scope.h"

// Constructor for a channel
Scope::Channel::Channel(void) {}

// Destructor for a channel
Scope::Channel::~Channel(void) {}

// Returns channel information
void *Scope::Channel::getInfo(void) {
	return info;
}

// Return read-only version of channel information
const void *Scope::Channel::getInfo(void) const {
	return info;
}

// Returns channel pen
QPen Scope::Channel::getPen(void) const {
	return pen;
}

// Returns channel scale
double Scope::Channel::getScale(void) const {
	return scale;
}

// Returns channel offset
double Scope::Channel::getOffset(void) const {
	return offset;
}

// Returns channel label
QString Scope::Channel::getLabel(void) const {
	return label;
}

// Scope constructor; inherits from QwtPlot
Scope::Scope(QWidget *parent) : QwtPlot(parent) {

	// Initialize director
	d_directPainter = new QwtPlotDirectPainter();
	setAutoReplot(false);

	// Set scope canvas
	setCanvas(new Canvas());
	plotLayout()->setAlignCanvasToScales(true);
	enableAxis(yLeft,false);
	enableAxis(xBottom,false);

	// Setup grid
	QwtPlotGrid *grid = new QwtPlotGrid();
	grid->setPen(Qt::black, 0.0, Qt::DotLine);
	grid->enableX(true);
	grid->enableXMin(true);
	grid->enableY(true);
	grid->enableYMin(true);
	grid->attach(this);

	// Initialize vars
	isPaused = false;
	drawZero = true;
	divX = 10;
	divY = 8;
	data_idx = 0;
	data_size = 100;
	hScl = 10.0;
	period = 1.0;
	dtLabel = "10ms";
	refresh = 250;
	triggering = false;
	triggerHolding = false;
	triggerDirection = NONE;
	triggerThreshold = 0.0;
	triggerHoldoff = 5.0;
	triggerLast = (size_t)(-1);
	triggerChannel = channels.end();

	// Timer controls refresh rate of scope
	timer = new QTimer;
	QObject::connect(timer,SIGNAL(timeout(void)),this,SLOT(timeoutEvent(void)));
	timer->start(refresh);
}

// Kill me
Scope::~Scope(void) {
	delete d_directPainter;
}

// Returns pause status of scope
bool Scope::paused(void) const {
	return isPaused;
}

// Pause toggle slot
void Scope::togglePause(void) {
	printf("pause called\n");
	isPaused = !isPaused;
}

// Timeout event slot
void Scope::timeoutEvent(void) {
	//if(!triggering)
	//update(drawForeground());
}

// Insert user specified channel into active list of channels with specified settings
std::list<Scope::Channel>::iterator Scope::insertChannel(QString label,double scale,double offset,const QPen &pen,void *info) {
	printf("inserting into list %d\n", channels.size());
	struct Channel channel;
	channel.label = label;
	channel.scale = scale;
	channel.offset = offset;
	channel.pen = pen;
	channel.info = info;
	channel.data.resize(data_size,0.0);
	channels.push_back(channel);
	//refreshBackground();
	return --channels.end();
}

// Remove user specified channel from active channels list
void *Scope::removeChannel(std::list<Scope::Channel>::iterator channel) {
	void *info = channel->info;
	channels.erase(channel);
	//refreshBackground();
	return info;
}

// Returns count of number of active channels
size_t Scope::getChannelCount(void) const {
	return channels.size();
}

// Returns beginning of channels list
std::list<Scope::Channel>::iterator Scope::getChannelsBegin(void) {
	return channels.begin();
}

// Returns end of channels list
std::list<Scope::Channel>::iterator Scope::getChannelsEnd(void) {
	return channels.end();
}

// Returns read-only pointer to beginning of channel list
std::list<Scope::Channel>::const_iterator Scope::getChannelsBegin(void) const {
	return channels.begin();
}

// Returns read-only pointer to end of channels list
std::list<Scope::Channel>::const_iterator Scope::getChannelsEnd(void) const {
	return channels.end();
}

// Zeros data
void Scope::clearData(void) {
	for(std::list<Channel>::iterator i = channels.begin(), end = channels.end();i != end;++i)
		for(size_t j = 0;j < data_size;++j)
			i->data[j] = 0.0;
}

// Scales data based upon desired settings for the channel
void Scope::setData(double data[],size_t size) {
	if(isPaused){
		return;
	}

	if(size < getChannelCount()) {
		ERROR_MSG("Scope::setData() : data size mismatch detected\n");
		return;
	}

	size_t index = 0;
	for(std::list<Channel>::iterator i = channels.begin(), end = channels.end();i != end;++i) {
		i->data[data_idx] = data[index++];

		if(triggering && i == triggerChannel &&
				((triggerDirection == POS && i->data[data_idx-1] < triggerThreshold && i->data[data_idx] > triggerThreshold) ||
				 (triggerDirection == NEG && i->data[data_idx-1] > triggerThreshold && i->data[data_idx] < triggerThreshold))) {
			triggerQueue.push_back(data_idx);
		}
	}

	++data_idx %= data_size;

	if(triggering && !triggerQueue.empty() && (data_idx+2)%data_size == triggerQueue.front()) {
		if(triggerLast != (size_t)(-1) && (triggerQueue.front()+data_size-triggerLast)%data_size*period < triggerHoldoff)
			triggerQueue.pop_front();
		else {
			triggerLast = triggerQueue.front();
			triggerQueue.pop_front();

			//if(!triggerHolding)
			//foreground = background;

			QPainterPath path;
			//QPainter painter(&foreground);

			size_t x, y;
			double scale;
			for(std::list<Channel>::iterator i = channels.begin(), iend = channels.end();i != iend;++i) {
				scale = height()/(i->scale*divY);
				//painter.setPen(i->getPen());
				x = 0;
				y = round(height()/2-scale*(i->data[data_idx]+i->offset));
				path.moveTo(x,y);
				for(size_t j = 1;j<i->data.size();++j) {
					x = round(((j*period)*width())/(hScl*divX));
					y = round(height()/2-scale*(i->data[(data_idx+j)%data_size]+i->offset));
					path.lineTo(x,y);
					//painter.drawPath(path);
					if(x >= width()) break;
				}
			}
			update();
		}
	}
}

// Returns the data size
size_t Scope::getDataSize(void) const {
	return data_size;
}

void Scope::setDataSize(size_t size) {
	for(std::list<Channel>::iterator i = channels.begin(), end = channels.end();i != end;++i) {
		i->data.clear();
		i->data.resize(size,0.0);
	}
	data_idx = 0;
	data_size = size;
	triggerQueue.clear();
}

Scope::trig_t Scope::getTriggerDirection(void) {
	return triggerDirection;
}

double Scope::getTriggerThreshold(void) {
	return triggerThreshold;
}

std::list<Scope::Channel>::iterator Scope::getTriggerChannel(void) {
	return triggerChannel;
}

bool Scope::getTriggerHolding(void) {
	return triggerHolding;
}

double Scope::getTriggerHoldoff(void) {
	return triggerHoldoff;
}

void Scope::setTrigger(trig_t direction,double threshold,std::list<Channel>::iterator channel,bool holding,double holdoff) {
	triggerHolding = holding;
	triggerHoldoff = holdoff;
	triggerLast = (size_t)(-1);

	if(triggerChannel != channel || triggerThreshold != threshold) {
		triggerChannel = channel;
		triggerThreshold = threshold;
		//refreshBackground();
	}

	if(triggerDirection != direction) {
		if(direction == NONE) {
			triggering = false;
			timer->start(refresh);
			triggerQueue.clear();
		} else {
			triggering = true;
			timer->stop();

			//		foreground = background;
			update();
		}
		triggerDirection = direction;
	}
}

double Scope::getDivT(void) const {
	return hScl;
}

void Scope::setDivT(double divT) {
	hScl = divT;
	QChar mu = QChar(0x3BC);
	if(divT >= 1000.)
		dtLabel = QString::number(divT*1e-3)+"s";
	else if(divT >= 1.)
		dtLabel = QString::number(divT)+"ms";
	else if(divT >= 1e-3)
		dtLabel = QString::number(divT*1e3)+mu+"s";
	else
		dtLabel = QString::number(divT*1e6)+"ns";

	//refreshBackground();
}

void Scope::setPeriod(double p) {
	period = p;
}

size_t Scope::getDivX(void) const {
	return divX;
}

size_t Scope::getDivY(void) const {
	return divY;
}

size_t Scope::getRefresh(void) const {
	return refresh;
}

void Scope::setRefresh(size_t r) {
	refresh = r;
	timer->setInterval(refresh);
}

void Scope::setChannelScale(std::list<Channel>::iterator channel,double scale) {
	channel->scale = scale;
}

void Scope::setChannelOffset(std::list<Channel>::iterator channel,double offset) {
	channel->offset = offset;
}

void Scope::setChannelPen(std::list<Channel>::iterator channel,const QPen &pen) {
	channel->pen = pen;
	//refreshBackground();
}

void Scope::setChannelLabel(std::list<Channel>::iterator channel,const QString &label) {
	channel->label = label;
	//refreshBackground();
}
