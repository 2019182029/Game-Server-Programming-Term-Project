npc_id = 99999;

function set_id(id)
	npc_id = id;
end

function do_npc_random_move()
	local old_vl = API_get_vl(npc_id);

	if 0 == #old_vl then
		API_do_npc_sleep(npc_id);
		return;
	end

	local new_vl = API_do_npc_random_move(npc_id, old_vl);

	for _, c_id in ipairs(new_vl) do
        if API_is_in_attack_range(npc_id, c_id) then
			API_register_event(npc_id, c_id, true);
			return;
		end
    end
	
	API_register_event(npc_id, -1, false);
end