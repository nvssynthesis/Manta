#pragma once
#include "Knob.h"

#include <array>

namespace gui
{
	class EQPad :
		public Comp,
		public Timer
	{
		enum Dimension { X, Y, NumDimensions };
		enum Tool { Select, Move, NumTools };

		struct Node
		{
			Node(Utils& u, PID xPID, PID yPID, PID scrollPID) :
				xyParam{ u.getParam(xPID), u.getParam(yPID) },
				scrollParam(u.getParam(scrollPID)),
				bounds(0.f, 0.f, 0.f, 0.f) ,
				x(getValue(X)),
				y(1.f - getValue(Y)),
				utils(u)
			{}

			void paint(Graphics& g) const
			{
				const auto thicc = utils.thicc;

				g.setColour(Colours::c(ColourID::Hover));
				g.fillEllipse(bounds);

				g.setColour(Colours::c(ColourID::Interact));
				g.drawEllipse(bounds, thicc);
			}

			bool valueChanged() noexcept
			{
				const auto _x = getValue(X);
				const auto _y = 1.f - getValue(Y);

				if (x != _x || y != _y)
				{
					x = _x;
					y = _y;
					return true;
				}

				return false;
			}

			float getValue(Dimension d) const noexcept
			{
				return xyParam[d]->getValue();
			}

			void beginGesture() const
			{
				for (auto xy = 0; xy < NumDimensions; ++xy)
					if(!xyParam[xy]->isInGesture())
						xyParam[xy]->beginGesture();
			}

			void onDrag(const float* dof, float speed)
			{
				for (auto xy = 0; xy < NumDimensions; ++xy)
				{
					auto param = xyParam[xy];
					
					const auto pol = -1.f + static_cast<float>(xy * 2);
					const auto newValue = juce::jlimit(0.f, 1.f, param->getValue() - dof[xy] * speed * pol);
					param->setValueNotifyingHost(newValue);
				}
			}

			void endGesture(bool wasDragged, bool altDown)
			{
				if(!wasDragged)
					if(altDown)
						for (auto xy = 0; xy < NumDimensions; ++xy)
						{
							auto& param = *xyParam[xy];
							param.setValueNotifyingHost(param.getDefaultValue());
						}
							
				
				for (auto xy = 0; xy < NumDimensions; ++xy)
					xyParam[xy]->endGesture();
			}

			void onScroll(float wheelDeltaY, bool isReversed, bool shiftDown)
			{
				const bool reversed = isReversed ? -1.f : 1.f;
				const bool isTrackPad = wheelDeltaY * wheelDeltaY < .0549316f;
				float dragY;
				if (isTrackPad)
					dragY = reversed * wheelDeltaY;
				else
				{
					const auto deltaYPos = wheelDeltaY > 0.f ? 1.f : -1.f;
					dragY = reversed * WheelDefaultSpeed * deltaYPos;
				}

				if (shiftDown)
					dragY *= SensitiveDrag;
				
				auto param = scrollParam;

				const auto newValue = juce::jlimit(0.f, 1.f, param->getValue() + dragY);
				param->setValueWithGesture(newValue);
			}

			std::array<Param*, NumDimensions> xyParam;
			Param* scrollParam;
			BoundsF bounds;
			float x, y;
		protected:
			Utils& utils;
		};

	public:
		EQPad(Utils& u, String&& _tooltip) :
			Comp(u, _tooltip, CursorType::Interact),
			nodes(),
			selected(),
			dragXY(),
			selectionLine(),
			selectionBounds(),
			tool(Tool::Select)
		{
			startTimerHz(PPDFPSKnobs);
		}

		void addNode(PID xParam, PID yParam, PID scrollParam)
		{
			nodes.push_back(
			{
				utils,
				xParam,
				yParam,
				scrollParam
			});
		}

		void paint(Graphics& g) override
		{
			const auto thicc = utils.thicc;

			g.setColour(Colours::c(ColourID::Txt));
			g.drawRoundedRectangle(bounds, thicc, thicc);

			for (const auto& node : nodes)
				node.paint(g);

			if (!selectionBounds.isEmpty())
			{
				g.setColour(Colours::c(ColourID::Hover));
				g.drawRoundedRectangle(denormalize(selectionBounds), thicc, thicc);
			}
			
			if (!selected.empty())
			{
				Stroke stroke(thicc, Stroke::PathStrokeType::JointStyle::curved, Stroke::PathStrokeType::EndCapStyle::butt);

				for (const auto& sel : selected)
				{
					const auto& node = *sel;

					g.setColour(Colours::c(ColourID::Interact));
					drawRectEdges(g, node.bounds, thicc, stroke);
				}
			}
		}
		
		void resized() override
		{
			bounds = getLocalBounds().toFloat().reduced(getNodeSize() * .5f);

			for (auto i = 0; i < numNodes(); ++i)
				moveNode(i);
		}

		void timerCallback() override
		{
			bool needsRepaint = false;
			
			for (auto i = 0; i < numNodes(); ++i)
			{
				auto& node = nodes[i];
				if (node.valueChanged())
				{
					moveNode(i);
					needsRepaint = true;
				}
			}

			if (needsRepaint)
				repaint();
		}

		size_t numNodes() noexcept
		{
			return nodes.size();
		}

		float getNodeSize() noexcept
		{
			return utils.thicc * 7.f;
		}

		void moveNode(int idx)
		{
			const auto x = bounds.getX();
			const auto y = bounds.getY();
			const auto w = bounds.getWidth();
			const auto h = bounds.getHeight();

			const auto nodeSize = getNodeSize();
			const auto nodeSizeHalf = nodeSize * .5f;

			auto& node = nodes[idx];

			const BoundsF nodeBounds
			(
				x + node.x * w - nodeSizeHalf,
				y + node.y * h - nodeSizeHalf,
				nodeSize,
				nodeSize
			);

			node.bounds = nodeBounds;
		}
		
		Node* getNode(const PointF& pos)
		{
			for (auto& node : nodes)
				if (node.bounds.contains(pos))
					return &node;

			return nullptr;
		}

		bool alreadySelected(const Node& node) const noexcept
		{
			for (const auto& sel : selected)
				if (sel == &node)
					return true;
			return false;
		}

		void mouseDown(const Mouse& mouse) override
		{
			auto node = getNode(mouse.position);
			bool clickedOnNode = node != nullptr;
			
			if (clickedOnNode)
			{
				tool = Tool::Move;
				dragXY = mouse.position;
				
				if (!alreadySelected(*node))
				{
					selected.clear();
					selected.push_back(node);
				}
				
				for (auto i = 0; i < selected.size(); ++i)
				{
					const auto& sel = *selected[i];
					sel.beginGesture();
				}
			}
			else
			{
				tool = Tool::Select;
				selected.clear();
				selectionLine.setStart
				(
					normalizeX(mouse.position.x),
					normalizeY(mouse.position.y)
				);
			}

			repaint();
		}

		void mouseDrag(const Mouse& mouse) override
		{
			if (tool == Tool::Select)
			{
				selectionLine.setEnd
				(
					normalizeX(mouse.position.x),
					normalizeY(mouse.position.y)
				);

				selectionBounds = smallestBoundsIn(selectionLine);

				updateSelected();
				repaint();
			}
			else if (tool == Tool::Move)
			{
				hideCursor();
				
				auto shiftDown = mouse.mods.isShiftDown();
				const auto speed = 1.f / utils.getDragSpeed();

				float pos[2] =
				{
					mouse.position.x,
					mouse.position.y
				};

				auto dragOffset = mouse.position - dragXY;
				if (shiftDown)
					dragOffset *= SensitiveDrag;

				const float dof[NumDimensions] = { dragOffset.x, dragOffset.y };

				for (auto s = 0; s < selected.size(); ++s)
				{
					auto& sel = *selected[s];
					sel.onDrag(dof, speed);
				}

				dragXY = mouse.position;
			}
		}

		void mouseUp(const Mouse& mouse) override
		{
			if (tool == Tool::Select)
			{
				selectionLine = { 0.f, 0.f, 0.f, 0.f };
				selectionBounds = { 0.f, 0.f, 0.f, 0.f };

				repaint();
			}
			else if (tool == Tool::Move)
			{
				bool wasDragged = mouse.mouseWasDraggedSinceMouseDown();

				for (auto i = 0; i < selected.size(); ++i)
				{
					auto& sel = *selected[i];
					sel.endGesture(wasDragged, mouse.mods.isAltDown());
				}

				if (wasDragged)
					showCursor(*this);
			}
		}
		
		void mouseWheelMove(const Mouse& mouse, const MouseWheel& wheel) override
		{
			if (selected.empty())
			{
				auto node = getNode(mouse.position);
				if (node != nullptr)
				{
					selected.push_back(node);
					repaint();
				}
			}
			else if (selected.size() == 1)
			{
				auto node = getNode(mouse.position);
				if(node != nullptr)
					if (!alreadySelected(*node))
					{
						selected[0] = node;
						repaint();
					}
			}

			for (auto s = 0; s < selected.size(); ++s)
			{
				auto& sel = *selected[s];
				sel.onScroll(wheel.deltaY, wheel.isReversed, mouse.mods.isShiftDown());
			}
		}

		void updateSelected()
		{
			for (auto& node : nodes)
			{
				const auto x = node.x;
				const auto y = node.y;

				bool nodeInSelection = selectionBounds.contains(x, y);

				if (nodeInSelection)
				{
					if (!alreadySelected(node))
						selected.push_back(&node);
				}
				else
					removeNode(node);
					
			}
		}

		void removeNode(const Node& node)
		{
			for (auto i = 0; i < selected.size(); ++i)
				if (selected[i] == &node)
				{
					selected.erase(selected.begin() + i);
					return;
				}
		}
		

		BoundsF bounds;
	protected:
		std::vector<Node> nodes;
		std::vector<Node*> selected;
		PointF dragXY;
		LineF selectionLine;
		BoundsF selectionBounds;
		Tool tool;
		
		float normalizeX(float value) const noexcept
		{
			return (value - bounds.getX()) / bounds.getWidth();
		}

		float normalizeY(float value) const noexcept
		{
			return (value - bounds.getY()) / bounds.getHeight();
		}

		float denormalizeX(float value) const noexcept
		{
			return bounds.getX() + value * bounds.getWidth();
		}

		float denormalizeY(float value) const noexcept
		{
			return bounds.getY() + value * bounds.getHeight();
		}

		PointF normalize(PointF pt) const noexcept
		{
			return
			{
				normalizeX(pt.x),
				normalizeY(pt.y)
			};
		}

		PointF denormalize(PointF pt) const noexcept
		{
			return
			{
				denormalizeX(pt.x),
				denormalizeY(pt.y)
			};
		}

		BoundsF normalize(BoundsF b) const
		{
			return
			{
				normalizeX(b.getX()),
				normalizeY(b.getY()),
				normalizeX(b.getWidth()),
				normalizeY(b.getHeight())
			};
		}

		BoundsF denormalize(BoundsF b) const
		{
			return
			{
				denormalizeX(b.getX()),
				denormalizeY(b.getY()),
				denormalizeX(b.getWidth()),
				denormalizeY(b.getHeight())
			};
		}
	};
}