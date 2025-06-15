KNIGHT = 4;
QUEEN = 5;

W_WIDTH = 2000;
W_HEIGHT = 2000;

npc_id = 99999;
npc_level = 99999;

function init_npc(id, level)
	npc_id = id;
	npc_level = level;
end

function do_npc_random_move()
	local old_vl = API_get_vl(npc_id);

	if 0 == #old_vl then
		API_do_npc_sleep(npc_id);
		return;
	end

	local x = API_get_x(npc_id);
	local y = API_get_y(npc_id);

	local directions = {
		{ dx = -2, dy = -1 }, { dx = -2, dy =  1 },
		{ dx = -1, dy = -2 }, { dx = -1, dy =  2 },
		{ dx =  1, dy = -2 }, { dx =  1, dy =  2 },
		{ dx =  2, dy = -1 }, { dx =  2, dy =  1 },
	};

	-- Shuffle
	for i = #directions, 2, -1 do
		local j = math.random(1, i)
		directions[i], directions[j] = directions[j], directions[i]
	end

	for _, move in ipairs(directions) do
		local new_x = x + move.dx
		local new_y = y + move.dy

		if API_is_valid_move(new_x, new_y) then
			local new_vl = API_do_npc_random_move(npc_id, new_x, new_y, old_vl);

			for _, c_id in ipairs(new_vl) do
				if API_is_in_attack_range(npc_id, c_id) then
					API_register_event(npc_id, c_id, true);
					return;
				end
			end
			
			API_register_event(npc_id, -1, false);
			return;
		end
	end
	
	API_do_npc_sleep(npc_id);
end

function do_npc_chase(target_id)
	local old_vl = API_get_vl(npc_id);
	
	-- Check Target is in Chase Range
	if not API_is_in_chase_range(npc_id, target_id) then
		local found = false;

		for _, c_id in ipairs(old_vl) do
			if 	API_is_in_chase_range(npc_id, c_id) then
				target_id = c_id;
				found = true;
				break;
			end
		end

		if not found then
			API_do_npc_sleep(npc_id);
			return;
		end
	end

	-- Check Target is in Attack Range
	if API_is_in_attack_range(npc_id, target_id) then
		API_register_event(npc_id, target_id, true);
		return;
	end

	-- A*
	local new_x, new_y = API_a_star(npc_id, target_id)

	if new_x == nil or new_y == nil then
		API_do_npc_sleep(npc_id);
		return;
	end

	API_do_npc_chase(npc_id, target_id, new_x, new_y, old_vl);
	
	API_register_event(npc_id, target_id, false);
end

function calc_attacked_coords(x, y)
	local pattern = {}

    if KNIGHT == npc_level then
        local deltas = {
            {-1, -2}, {1, -2}, {-1, 2}, {1, 2},
            {-2, -1}, {-2, 1}, {2, -1}, {2, 1}
        };

        for _, d in ipairs(deltas) do
            local dx, dy = d[1], d[2];
            local nx, ny = x + dx, y + dy;

            if nx >= 0 and nx < W_WIDTH and ny >= 0 and ny < W_HEIGHT then
                table.insert(pattern, {nx, ny});
            end
        end
    elseif QUEEN == npc_level then
        for dx = -1, 1 do
            for dy = -1, 1 do
                if not (dx == 0 and dy == 0) then
                    local nx, ny = x + dx, y + dy;

                    if nx >= 0 and nx < W_WIDTH and ny >= 0 and ny < W_HEIGHT then
                        table.insert(pattern, {nx, ny});
                    end
                end
            end
        end
    end

    return pattern
end

function do_npc_attack(target_id)
	local x = API_get_x(npc_id);
	local y = API_get_y(npc_id);

	local attacked_coords = calc_attacked_coords(x, y);

	API_do_npc_attack(npc_id, target_id, attacked_coords);
	
	API_register_event(npc_id, target_id, false);
end
